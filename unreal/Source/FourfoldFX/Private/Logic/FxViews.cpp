// FourfoldFX logic island - persistent body views. Owner: stream `fx`.
#include "FxViews.h"

#include "FxFracture.h"
#include "FxMeshLib.h"

#include <cmath>

namespace ffx {

namespace {

using ff::Form;
using ff::Mat;
using ff::Phase;

constexpr float kTierScale[4] = {1.0f, 1.25f, 1.5f, 1.8f};

float SeedParam(uint32_t seed) { return static_cast<float>(seed % 977u) + 0.5f; }

// Projectiles face their travel (BodyViews._orient_along): lances / needles +Y along it, discs fly flat and bank,
// plates turn their face (+Z) to it.
Basis OrientAlong(const Vec3& vel, const char* style) {
	const Vec3 f = Norm(vel, Vec3(0.0f, 0.0f, 1.0f));
	Vec3 x = f.cross(Vec3(0.0f, 1.0f, 0.0f));
	x = x.length_squared() > 1e-6f ? Norm(x) : Vec3(1.0f, 0.0f, 0.0f);
	const std::string_view s(style);
	if (s == "lance" || s == "rod") {
		const Vec3 z = Norm(x.cross(f));
		return {x, f, z};
	}
	if (s == "disc") {
		const Vec3 up = Norm(Vec3(0.0f, 1.0f, 0.0f) + x * 0.25f);
		return {x, up, Norm(x.cross(up))};
	}
	return {x, Norm(f.cross(x)), f};
}

std::vector<Vec3> PathWithHead(const ff::BodyView& b, const Vec3& head, bool appendHead) {
	std::vector<Vec3> pts(b.wave_path.begin(), b.wave_path.end());
	if (appendHead && (pts.empty() || pts.back().distance_to(head) > 0.05f)) pts.push_back(head);
	return pts;
}

// --------------------------------------------------------------------------------------------- stone
class StoneView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		keyLight_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
		Rng r(seed_);
		rot_ = Basis::AxisAngle(r.OnSphere(), r.Range(0.0f, kFxTau));
		radius_ = MaxF(f.b.radius, 0.01f);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		pos_ = f.p;
		radius_ = Lerp(radius_, MaxF(b.radius, 0.01f), Sat(c.dt * 10.0f));
		heat_ = Heat01(b);
		melt_ = Sat(b.liquid);
		crust_ = Crust01(b);
		tier_ = ClampI(MaxI(b.tier, tierHint), 0, 3);
		spear_ = b.tag == "spear";
		vel_ = b.vel;
		const float spd = b.vel.length();
		if (b.controller >= 0) {
			rot_ = Basis::YawY(1.2f * c.dt).Mul(rot_);
		} else if (spear_) {
			if (spd > 1.0f) spearBasis_ = OrientAlong(b.vel, "plate");
		} else if (!b.on_ground && spd >= 1.0f && c.dt > 0.0f) {
			// tumble from the linear velocity: angular speed v / r about up x v
			const Vec3 axis = Vec3(0.0f, 1.0f, 0.0f).cross(b.vel);
			if (axis.length_squared() > 1e-6f) {
				const float w = spd / MaxF(b.radius, 0.08f) * (b.tag == "crag" ? 0.55f : 0.8f);
				rot_ = Basis::AxisAngle(axis, w * c.dt).Mul(rot_).Orthonormal();
			}
		}
		// a charged throw leaves a tier-tinted streak while it flies (MOVESET §11.2)
		const bool flying = tier_ > 0 && b.attack_id != 0 && b.controller < 0 && spd > 4.0f;
		if (flying && !trailOn_) {
			trail_.Begin(c, radius_ * (1.3f + 0.35f * static_cast<float>(tier_)), c.cfg.TierColor(tier_),
			             0.28f + 0.14f * static_cast<float>(tier_), 0.18f + 0.12f * static_cast<float>(tier_));
			trailOn_ = true;
		}
		if (trailOn_ && flying) trail_.Push(static_cast<float>(c.time), pos_);
		if (trailOn_ && !flying) trail_.End();
	}
	void Draw(Ctx& c, float fade) override {
		DrawItem& it = c.out.AddStatic(key_, MatSlot::Rock, RockAsset(seed_), Place(fade), &meshlib::Rock(seed_));
		Look(it.params, fade);
		it.castShadow = true;
		const float glow = Sat(0.75f * heat_ + 0.45f * melt_) * (1.0f - 0.8f * crust_) * fade;
		if (glow > 0.12f)
			c.out.Light(keyLight_, pos_, Color(1.0f, 0.36f + 0.3f * glow, 0.1f + 0.12f * glow),
			            glow * glow * 1.5f * Clamp(radius_ / 0.4f, 0.5f, 2.0f), radius_ * 7.0f, 1.0f);
		if (trailOn_ && !trail_.Draw(c, fade)) trailOn_ = false;
	}
	float FadeTime() const override { return 0.15f; }
	bool Break(Ctx& c) override {
		// a molten blob splashes rather than breaking; the sim's own rubble stones live on beside the pieces
		if (!c.in.physicsDebris || c.q.rockPieces < 2 || melt_ > 0.5f) return false;
		const FractureSettings& fs = c.cfg.fracture;
		FractureReq& r = c.out.Fracture();
		r.pieces = &meshlib::RockPieces(seed_, c.q.rockPieces);
		r.xform = Place(1.0f);
		r.mat = MatSlot::Rock;
		Look(r.params, 1.0f);
		r.vel = vel_ * 0.4f;
		r.origin = pos_;
		r.burst = fs.rockBurst * (0.8f + 0.1f * static_cast<float>(tier_));
		r.scale = fs.rockScale;
		r.life = fs.rockLife;
		r.seed = HashCombine(seed_, static_cast<uint32_t>(c.in.curr ? c.in.curr->tick : 0));
		return true;
	}

private:
	Xform Place(float fade) const {
		const float s = radius_ * (spear_ ? 1.0f : kTierScale[tier_]) * (fade < 1.0f ? 0.5f + 0.5f * fade : 1.0f);
		Xform x;
		x.pos = pos_;
		x.basis = spear_ ? spearBasis_.Scaled(s, s, s * 2.4f) : rot_.Scaled(s);
		return x;
	}
	void Look(ParamBlock& p, float fade) const {
		p.Set(P::Seed, SeedParam(seed_));
		p.Set(P::Heat, heat_);
		p.Set(P::Melt, melt_);
		p.Set(P::Crust, crust_);
		p.Set(P::Rise, 1.0f);
		p.Set(P::Detail, 1.0f);
		p.Set(P::Fade, fade);
	}

	uint32_t key_ = 0, keyLight_ = 0, seed_ = 0;
	Basis rot_, spearBasis_;
	Vec3 pos_, vel_;
	float radius_ = 0.3f, heat_ = 0.0f, melt_ = 0.0f, crust_ = 0.0f;
	int tier_ = 0;
	bool spear_ = false, trailOn_ = false;
	Trail trail_;
};

// --------------------------------------------------------------------------------------------- strips (lava / water / sand / rime / mud)
class StripView final : public BodyView {
public:
	StripView(ViewSel s, StripStyle st) : BodyView(std::move(s)), style_(st) {}
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		keyLight_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		if (sel.kind == ViewKind::LavaPool) {
			const float r3 = MaxF(b.zone_radius, 0.5f);
			const Vec3 sig(b.pos.x + r3 * 1000.0f, b.pos.y, b.pos.z);
			if (sig != sig_) {
				sig_ = sig;
				const float gy = c.Ground(b.pos);
				const Vec3 cc(b.pos.x, gy - 0.1f, b.pos.z);
				const Vec3 d(r3 * 0.45f, 0.0f, r3 * 0.2f);
				Build(c, {cc - d, cc, cc + d}, {r3 * 1.5f, r3 * 1.9f, r3 * 1.6f});
			}
			melt_ = Sat(b.liquid > 0.0f ? b.liquid : Life01(b));
			crust_ = Sat(1.0f - melt_ / 0.85f);
			speed_ = 0.15f;
			front_ = b.pos + Vec3(0.0f, 0.2f, 0.0f);
		} else {
			// A moving wave's front is interpolated every frame; a settled ridge is rebuilt only when it changes.
			const bool moving = b.form == Form::Wave;
			Vec3 sig(1e9f, 0.0f, 0.0f);
			if (!moving && b.wave_path.size() >= 2)
				sig = Vec3(static_cast<float>(b.wave_path.size()) + b.wave_width * 100.0f, b.wave_path.back().x, b.wave_path.back().z);
			if (moving || sig != sig_) {
				sig_ = sig;
				std::vector<Vec3> pts = PathWithHead(b, f.p, moving);
				if (pts.size() < 2) pts.insert(pts.begin(), f.p - Norm(b.wave_dir, Vec3(0, 0, 1)) * 0.4f);
				std::vector<float> w(pts.size());
				for (size_t k = 0; k < pts.size(); ++k) {
					const float t = static_cast<float>(k) / MaxF(1.0f, static_cast<float>(pts.size() - 1));
					w[k] = b.wave_width * Lerp(0.55f, 1.0f, t);
				}
				Build(c, pts, w);
				front_ = pts.back();
			}
			melt_ = Sat(b.liquid);
			crust_ = b.liquid <= 0.0f ? 1.0f : Sat(1.0f - b.liquid / 0.85f);
			speed_ = b.vel.length();
			if (style_ == StripStyle::Water) {
				// a water wave freezes in place (rime): crust = frozen fraction
				crust_ = b.phase == Phase::Frozen ? 1.0f : Sat(1.0f - b.liquid);
				melt_ = Sat(b.liquid);
			}
		}
		const float live = 1.0f - crust_;
		phase_ += speed_ * (1.0f - 0.92f * crust_) * c.dt;
		boil_ += c.dt * (0.25f + 0.75f * live);
	}
	void Draw(Ctx& c, float fade) override {
		if (mesh_.Empty()) return;
		MatSlot slot = MatSlot::LavaStrip;
		if (style_ == StripStyle::Water) slot = MatSlot::Water;
		else if (style_ != StripStyle::Lava) slot = MatSlot::GroundStrip;
		DrawItem& it = c.out.Add(key_, slot, &mesh_);
		it.params.Set(P::Melt, melt_);
		it.params.Set(P::Crust, crust_);
		it.params.Set(P::Flow, phase_);
		it.params.Set(P::Boil, boil_);
		it.params.Set(P::Seed, static_cast<float>(seed_ % 53u) * 0.173f);
		it.params.Set(P::Fade, fade);
		switch (style_) {
			case StripStyle::Water:
				it.params.Set(P::Shape, 2.0f);   // water material: strip with a foam lip
				it.params.Set(P::Frozen, crust_);
				it.sortPriority = 0;
				break;
			case StripStyle::Sand: it.params.Set(P::Style, 0.0f); break;
			case StripStyle::Rime: it.params.Set(P::Style, 1.0f); break;
			case StripStyle::Mud: it.params.Set(P::Style, 2.0f); break;
			default: break;
		}
		if (style_ == StripStyle::Lava) {
			const float glow = melt_ * (1.0f - 0.8f * crust_) * fade;
			if (glow > 0.15f)
				c.out.Light(keyLight_, front_ + Vec3(0.0f, 0.35f, 0.0f), Color(1.0f, 0.42f, 0.12f), 1.4f * glow,
				            sel.kind == ViewKind::LavaPool ? 6.0f : 5.0f, 1.2f);
		}
	}
	float FadeTime() const override { return 0.3f; }

private:
	void Build(Ctx& c, const std::vector<Vec3>& pts, const std::vector<float>& w) {
		const StripLook& sl = c.cfg.Strip(style_);
		StripParams sp;
		sp.heightFront = sl.heightFront;
		sp.heightTail = sl.heightTail;
		sp.frontBulge = sl.bulge;
		mesh_.Clear();
		AppendPathStrip(mesh_, pts, w, sp);
		mesh_.Commit();
	}
	StripStyle style_;
	uint32_t key_ = 0, keyLight_ = 0, seed_ = 0;
	Vec3 sig_{1e9f, 1e9f, 1e9f};
	Vec3 front_;
	float melt_ = 1.0f, crust_ = 0.0f, speed_ = 0.0f, phase_ = 0.0f, boil_ = 0.0f;
	MeshData mesh_;
};

// --------------------------------------------------------------------------------------------- walls
class WallView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		keyCollider_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		pos_ = b.pos;
		yaw_ = b.wall_yaw;
		half_ = b.wall_half;
		const float prev = rise_;
		rise_ = Sat(b.wall_rise);
		damage_ = Sat(b.wall_damage);
		heat_ = WallHeat01(b);
		// dust puffs at the base while it rises (two stages)
		if (rise_ > prev) {
			const Vec3 right = Basis::YawY(yaw_).x;
			auto puff = [&](float k) {
				for (float side : {-0.55f, 0.55f})
					c.fx.Dust(c, pos_ + right * (half_.x * side), Vec3(0.0f, 1.0f, 0.0f), k, c.cfg.DustColor(Fam::Stone));
			};
			if (dustStage_ == 0 && rise_ > 0.04f) {
				dustStage_ = 1;
				puff(1.2f);
			} else if (dustStage_ == 1 && rise_ > 0.5f) {
				dustStage_ = 2;
				puff(0.8f);
			}
		}
		if (rise_ <= 0.001f) dustStage_ = 0;
	}
	void Draw(Ctx& c, float fade) override {
		if (broken_) return;   // its physics pieces took its place
		Xform x;
		x.pos = pos_;
		x.basis = Frame();
		DrawItem& it = c.out.Add(key_, MatSlot::Rock, &meshlib::Wall(seed_), x);
		Look(it.params, rise_ * fade);   // a crumbling wall sinks back into the ground
		it.castShadow = true;
		// physics debris bounces off a raised wall (the unit wall spans x -1..1, y 0..1, z -0.27..0.27)
		const float h = MaxF(half_.y, 0.1f) * rise_ * fade;
		if (c.in.physicsDebris && h > 0.15f) {
			ColliderReq& col = c.out.colliders.emplace_back();
			col.key = keyCollider_;
			col.xform.pos = pos_ + Vec3(0.0f, h, 0.0f);
			col.xform.basis = Basis::YawY(yaw_).Scaled(MaxF(half_.x, 0.15f), h, MaxF(half_.z * 4.0f, 0.3f) * 0.27f);
		}
	}
	float FadeTime() const override { return 0.35f; }
	bool Break(Ctx& c) override {
		// sand and mud slump into dust; a wall that has barely risen just sinks
		const std::string& st = sel.style;
		if (!c.in.physicsDebris || c.q.wallPieces < 1 || st == "sand" || st == "mud" || rise_ < 0.3f) return false;
		const FractureSettings& fs = c.cfg.fracture;
		FractureReq& r = c.out.Fracture();
		r.pieces = &meshlib::WallPieces(seed_, c.q.wallPieces);
		const Basis b = Frame();
		r.xform.basis = b;
		r.xform.pos = pos_ - b.y * (1.0f - rise_);   // a half-risen wall breaks where it stands
		r.mat = MatSlot::Rock;
		Look(r.params, 1.0f);
		r.origin = pos_ - b.y * 0.6f;   // below the base centre: pieces lean out and topple
		r.burst = fs.wallBurst;
		r.scale = fs.wallScale;
		r.life = fs.wallLife;
		r.seed = HashCombine(seed_, static_cast<uint32_t>(c.in.curr ? c.in.curr->tick : 0));
		broken_ = true;
		return true;
	}

private:
	Basis Frame() const {
		return Basis::YawY(yaw_).Scaled(MaxF(half_.x, 0.15f), MaxF(half_.y * 2.0f, 0.2f), MaxF(half_.z * 4.0f, 0.3f));
	}
	void Look(ParamBlock& p, float rise) const {
		p.Set(P::Seed, SeedParam(seed_));
		p.Set(P::Rise, rise);
		p.Set(P::RiseHeight, 1.25f);
		p.Set(P::Detail, 2.2f);
		p.Set(P::Damage, damage_);
		p.Set(P::Heat, heat_);
		p.Set(P::Fade, 1.0f);
		const std::string& st = sel.style;
		if (st == "obsidian") {
			p.Set(PV::Tint, Color(0.32f, 0.30f, 0.36f));
			p.Set(P::Glass, 1.0f);
		} else if (st == "sand") {
			p.Set(PV::Tint, Color(2.7f, 2.15f, 1.45f));
			p.Set(P::Glass, 0.0f);
		} else if (st == "mud") {
			p.Set(PV::Tint, Color(0.95f, 0.72f, 0.5f));
			p.Set(P::Glass, 0.45f);
		} else {
			p.Set(PV::Tint, Color(1.35f, 1.25f, 1.1f));   // pale earth: reads against the scanned courtyard stone
			p.Set(P::Glass, 0.0f);
		}
	}

	uint32_t key_ = 0, keyCollider_ = 0, seed_ = 0;
	Vec3 pos_, half_{1.0f, 0.6f, 0.25f};
	float yaw_ = 0.0f, rise_ = 0.0f, damage_ = 0.0f, heat_ = 0.0f;
	int dustStage_ = 0;
	bool broken_ = false;
};

// --------------------------------------------------------------------------------------------- water blob / ribbon / puddle
class WaterView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		keyMark_ = c.keys.New();
		radius_ = MaxF(f.b.radius, 0.02f);
		// a held pressure jet: the body sits mid-jet with radius = half its length (conduction); draw a water tube
		// from the caster's chest through it instead of a giant orb
		jet_ = f.b.tag == "jet";
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		pos_ = f.p;
		radius_ = Lerp(radius_, MaxF(b.radius, 0.02f), Sat(c.dt * 12.0f));
		frozen_ = Sat(1.0f - b.liquid);
		flow_ += c.dt * (1.0f - frozen_);
		if (jet_ && b.controller >= 0) {
			const Vec3 chest = c.ActorPos(b.controller) + Vec3(0.0f, 1.3f, 0.0f);
			const Vec3 d = f.p - chest;
			const float half = d.length();
			if (half > 0.05f) {
				const Vec3 dir = d / half;
				const Vec3 side = Perp(dir);
				std::vector<Vec3> pts;
				std::vector<float> rad;
				const int n = 12;
				for (int k = 0; k < n; ++k) {
					const float t = static_cast<float>(k) / static_cast<float>(n - 1);
					const float wob = std::sin(t * 9.0f - flow_ * 22.0f) * 0.025f * t;
					pts.push_back(chest + dir * (0.35f + t * (2.0f * half - 0.35f)) + side * wob);
					rad.push_back(Lerp(0.055f, 0.11f, t));
				}
				mesh_.Clear();
				AppendTube(mesh_, pts, rad, 10, true, true, 2);
				mesh_.Commit();
			}
			return;
		}
		if (sel.kind == ViewKind::Ribbon) {
			std::vector<Vec3> pts(trail.begin(), trail.end());
			pts.push_back(f.p);
			if (pts.size() >= 2) {
				std::vector<float> rad(pts.size());
				for (size_t k = 0; k < pts.size(); ++k)
					rad[k] = b.radius * Lerp(0.35f, 1.0f, static_cast<float>(k) / MaxF(1.0f, static_cast<float>(pts.size() - 1)));
				mesh_.Clear();
				AppendTube(mesh_, pts, rad, 10, true, true, 3);
				mesh_.Commit();
			}
		}
		if (sel.kind == ViewKind::Puddle) {
			pos_ = b.pos;
			radius_ = MaxF(b.radius, 0.05f);
		}
	}
	void Draw(Ctx& c, float fade) override {
		if (sel.kind == ViewKind::Puddle) {
			// a dark wet mark with a thin calm water lens over it (the mark alone vanishes at low camera angles)
			Xform xm;
			xm.pos = pos_ + Vec3(0.0f, 0.012f, 0.0f);
			xm.basis = Basis::Identity().Scaled(radius_, 1.0f, radius_);
			DrawItem& mark = c.out.Add(keyMark_, MatSlot::Ground, &meshlib::GroundQuad(), xm);
			mark.params.Set(P::Style, 8.0f);   // wet
			mark.params.Set(P::Fade, fade);
			mark.params.Set(P::Seed, static_cast<float>(body % 41) * 0.123f);
			mark.sortPriority = -3;
			const float rl = radius_ * 0.78f;
			Xform xl;
			xl.pos = pos_ + Vec3(0.0f, 0.018f, 0.0f);
			xl.basis = Basis::Identity().Scaled(rl, 0.025f, rl);
			DrawItem& lens = c.out.Add(key_, MatSlot::Water, &meshlib::Sphere(2), xl);
			lens.params.Set(P::Shape, 1.0f);
			lens.params.Set(P::Detail, 0.2f);   // calm
			lens.params.Set(P::Frozen, frozen_);
			lens.params.Set(P::Flow, flow_);
			lens.params.Set(P::Fade, fade);
			lens.sortPriority = -2;
			return;
		}
		if (sel.kind == ViewKind::Ribbon || jet_) {
			if (mesh_.Empty()) return;
			DrawItem& it = c.out.Add(key_, MatSlot::Water, &mesh_);
			it.params.Set(P::Shape, 0.0f);
			it.params.Set(P::Frozen, frozen_);
			it.params.Set(P::Flow, flow_);
			it.params.Set(P::Fade, fade);
			return;
		}
		Xform x;
		x.pos = pos_;
		x.basis = Basis::Identity().Scaled(radius_ * (0.4f + 0.6f * fade));
		DrawItem& it = c.out.Add(key_, MatSlot::Water, &meshlib::Sphere(2), x);
		it.params.Set(P::Shape, 1.0f);
		it.params.Set(P::Detail, 1.0f);
		it.params.Set(P::Frozen, frozen_);
		it.params.Set(P::Flow, flow_);
		it.params.Set(P::Fade, fade);
	}
	float FadeTime() const override { return sel.kind == ViewKind::Puddle ? 0.5f : 0.12f; }

private:
	uint32_t key_ = 0, keyMark_ = 0;
	Vec3 pos_;
	float radius_ = 0.3f, frozen_ = 0.0f, flow_ = 0.0f;
	bool jet_ = false;
	MeshData mesh_;
};

// --------------------------------------------------------------------------------------------- metal
class MetalView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		key_ = c.keys.New();
		keyBlur_ = c.keys.New();
		keyLight_ = c.keys.New();
		seed_ = SeedOf(b.id);
		const std::string& st = sel.style;
		size_ = Vec3(1.0f, 1.0f, 1.0f) * MaxF(b.radius, 0.12f);
		if (st == "disc") size_ = Vec3(1.0f, 1.0f, 1.0f) * Clamp(b.radius * 1.1f, 0.14f, 0.45f);
		else if (st == "lance" || st == "rod") {
			const float rx = Clamp(b.radius * 0.18f, 0.025f, 0.07f);
			size_ = Vec3(rx, Clamp(b.radius * 6.0f, 0.9f, 2.2f), rx);
		} else if (st == "planted") size_ = Vec3(0.06f, 2.2f, 0.06f);
		else if (st == "plate") {
			if (b.form == Form::Wall) size_ = Vec3(b.wall_half.x * 2.0f, b.wall_half.y * 2.0f, MaxF(b.wall_half.z * 0.5f, 0.05f));
			else size_ = Vec3(b.radius * 2.2f, b.radius * 0.18f, b.radius * 2.2f);
		} else if (st == "caltrops") {
			Rng r(static_cast<uint64_t>(seed_) * 31u + 7u);
			const float fr = MaxF(b.zone_radius, 0.4f);
			const int n = ClampI(static_cast<int>(fr * fr * 3.0f) + 4, 4, 14);
			field_.Clear();
			for (int i = 0; i < n; ++i) {
				const float a = r.F01() * kFxTau, rr = std::sqrt(r.F01()) * fr;
				MeshData one = meshlib::Caltrop();
				one.Transform({Vec3(std::cos(a) * rr, 0.04f, std::sin(a) * rr),
				               Basis::YawY(r.F01() * kFxTau).Scaled(r.Range(0.15f, 0.21f))});
				field_.Append(one);
			}
			field_.Commit();
		}
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		const std::string& st = sel.style;
		pos_ = f.p;
		heat_ = MaxF(Heat01(b), Sat(b.liquid));
		spin_ = b.spin;
		angle_ = std::fmod(angle_ + spin_ * c.dt, kFxTau);
		if (st == "planted") {
			pos_ = Vec3(b.pos.x, c.Ground(b.pos) + 1.0f, b.pos.z);
			basis_ = Basis::AxisAngle(Vec3(0.0f, 0.0f, 1.0f), 0.04f);
		} else if (st == "caltrops") {
			pos_ = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
			basis_ = Basis::Identity();
		} else if (b.form == Form::Wall) {
			pos_ = b.pos + Vec3(0.0f, b.wall_half.y * (2.0f * b.wall_rise - 1.0f), 0.0f);
			basis_ = Basis::YawY(b.wall_yaw);
		} else if (b.vel.length_squared() > 0.5f && b.controller < 0) {
			basis_ = OrientAlong(b.vel, st == "disc" ? "disc" : (st == "lance" || st == "rod" ? "lance" : "plate"));
		} else if (b.controller >= 0) {
			basis_ = Basis::YawY(1.8f * c.dt).Mul(basis_);
		}
	}
	void Draw(Ctx& c, float fade) override {
		const std::string& st = sel.style;
		const float k = fade < 1.0f ? 0.5f + 0.5f * fade : 1.0f;
		Xform x;
		x.pos = pos_;
		DrawItem* it = nullptr;
		if (st == "caltrops") {
			it = &c.out.Add(key_, MatSlot::Metal, &field_, x);
		} else if (st == "disc") {
			x.basis = basis_.Mul(Basis::YawY(angle_)).Scaled(size_.x * k);
			it = &c.out.AddStatic(key_, MatSlot::Metal, MeshAsset::Disc, x, &meshlib::Disc());
			// spin blur ring in the disc plane
			const float blur = Sat((std::fabs(spin_) - 6.0f) / 30.0f);
			if (blur > 0.02f) {
				Xform xb;
				xb.pos = pos_;
				xb.basis = basis_.Scaled(size_.x * 1.25f, 1.0f, size_.x * 1.25f);
				DrawItem& r = c.out.Add(keyBlur_, MatSlot::Ring, &meshlib::GroundQuad(), xb);
				r.params.Set(P::Style, 3.0f);
				r.params.Set(P::Radius, 0.82f);
				r.params.Set(P::Width, 0.16f);
				r.params.Set(P::Cover, 0.55f);
				r.params.Set(PV::Color, Linear(Color(0.75f, 0.82f, 0.92f)));
				r.params.Set(P::Opacity, blur * 0.8f * fade);
				r.params.Set(P::Phase, angle_);
				r.params.Set(P::Glow, 1.0f);
			}
		} else if (st == "lance" || st == "rod" || st == "planted") {
			x.basis = basis_.Scaled(size_.x * k, size_.y * k, size_.z * k);
			if (st == "lance") it = &c.out.AddStatic(key_, MatSlot::Metal, MeshAsset::Lance, x, &meshlib::Lance());
			else it = &c.out.Add(key_, MatSlot::Metal, &meshlib::Rod(), x);
		} else if (st == "plate") {
			static const MeshData kBox = [] {
				MeshData m;
				AppendChamferBox(m, Vec3(), Vec3(0.5f, 0.5f, 0.5f), 0.04f, Basis::Identity());
				m.Commit();
				return m;
			}();
			x.basis = basis_.Scaled(size_.x, size_.y * k, size_.z);
			it = &c.out.AddStatic(key_, MatSlot::Metal, MeshAsset::Plate, x, &kBox);
		} else {
			x.basis = basis_.Scaled(size_.x * k);
			it = &c.out.Add(key_, MatSlot::Metal, &meshlib::Sphere(1), x);
		}
		it->params.Set(P::Heat, heat_);
		it->params.Set(P::Seed, static_cast<float>(seed_ % 101u) * 0.1f);
		it->params.Set(P::Spin, spin_);
		it->params.Set(P::Fade, fade);
		it->castShadow = st != "caltrops";
		if (heat_ > 0.45f)
			c.out.Light(keyLight_, pos_, Color(1.0f, 0.35f, 0.12f), (heat_ - 0.45f) * 1.6f * fade, 2.5f, 0.6f);
	}

private:
	uint32_t key_ = 0, keyBlur_ = 0, keyLight_ = 0, seed_ = 0;
	Vec3 pos_, size_{1, 1, 1};
	Basis basis_;
	float heat_ = 0.0f, spin_ = 0.0f, angle_ = 0.0f;
	MeshData field_;
};

// --------------------------------------------------------------------------------------------- clouds
class CloudView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		CloudStyle cs = CloudStyle::Mist;
		for (int i = 0; i < kNumCloudStyles; ++i)
			if (kCloudStyleNames[static_cast<size_t>(i)] == sel.style) cs = static_cast<CloudStyle>(i);
		cloud_.Configure(c, cs, SeedOf(f.b.id));
		style_ = cs;
		m0_ = MaxF(f.b.mass, 0.01f);
		keyLoop_ = c.keys.New();
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		const std::string& st = sel.style;
		x_ = Xform();
		if (st == "slug") {
			x_.pos = f.p;
			if (b.vel.length_squared() > 0.25f) {
				const Vec3 back = Norm(b.vel * -1.0f);
				// local -Z (the slug trail) behind the travel: +Z along the velocity
				x_.basis = Basis::FromFwdUp(back * -1.0f, std::fabs(back.y) < 0.95f ? Vec3(0, 1, 0) : Vec3(1, 0, 0));
				slugBasis_ = x_.basis;
			} else {
				x_.basis = slugBasis_;
			}
			cloud_.SetShape(MaxF(b.radius * 1.2f, 0.18f), MaxF(b.radius * 1.6f, 0.25f));
		} else {
			const float gy = c.Ground(b.pos);
			x_.pos = Vec3(f.p.x, (b.form == Form::Zone || st != "steam") ? gy : MinF(f.p.y, gy + 0.2f), f.p.z);
			float r = b.zone_radius > 0.0f ? b.zone_radius : MaxF(b.radius, 0.6f);
			float h = r * 0.8f, sq = 1.0f;
			if (st == "fog" || st == "mist") h = Clamp(r * 0.45f, 0.6f, 1.6f);
			else if (st == "sandstorm") h = Clamp(r * 0.9f, 1.5f, 3.5f);
			else if (st == "steam") h = Clamp(r * 1.2f, 1.2f, 3.0f);
			else if (st == "geyser") {
				h = Clamp(2.5f + 0.6f * static_cast<float>(b.tier), 2.5f, 5.0f);
				r = Clamp(r * 0.4f, 0.4f, 1.0f);
			} else if (st == "steam_screen") {
				h = 2.4f;
				sq = 2.2f;
				r = Clamp(r * 0.5f, 0.6f, 1.6f);
				x_.basis = Basis::YawY(b.wall_yaw);
			} else if (st == "dust_line") {
				h = 0.7f;
				r = MaxF(b.radius, 0.6f);
			}
			cloud_.SetShape(r, h, sq);
		}
		amount_ = b.form == Form::Zone ? Life01(b) : Clamp(b.mass / m0_, 0.25f, 1.0f);
		cloud_.SetAmount(amount_);
		radius_ = b.zone_radius > 0.0f ? b.zone_radius : MaxF(b.radius, 0.6f);
	}
	void Draw(Ctx& c, float fade) override {
		// steam / smoke columns get a persistent Niagara plume rising from the base (fx_config "niagara_loops")
		const bool steam = style_ == CloudStyle::Steam || style_ == CloudStyle::Geyser || style_ == CloudStyle::SteamScreen;
		const LCue cue = steam ? LCue::Steam : LCue::Smoke;
		if ((steam || style_ == CloudStyle::Smoke) && fade >= 1.0f && amount_ > 0.1f && c.LoopOn(cue)) {
			LoopReq& l = c.out.Loop(keyLoop_, cue, x_.pos);
			l.scale = Clamp(radius_ * 0.8f, 0.6f, 2.0f) * (style_ == CloudStyle::Geyser ? 1.4f : 1.0f);
			l.intensity = amount_;
			l.color = c.cfg.Cloud(style_).top;
		}
		cloud_.Draw(c, x_, fade);
	}
	float FadeTime() const override { return 0.4f; }

private:
	PuffCloud cloud_;
	Xform x_;
	Basis slugBasis_;
	CloudStyle style_ = CloudStyle::Mist;
	float m0_ = 1.0f, amount_ = 1.0f, radius_ = 1.0f;
	uint32_t keyLoop_ = 0;
};

// --------------------------------------------------------------------------------------------- crystals
class CrystalView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
		glass_ = sel.style == "glass";
		const ff::BodyView& b = f.b;
		if (sel.kind == ViewKind::CrystalWall) {
			mode_ = (b.tag == "ridge" || b.tag == "rime") ? CrystalMode::Ridge : CrystalMode::Wall;
		} else {
			mode_ = CrystalMode::Shard;
			const float r = MaxF(b.radius, 0.06f);
			size_ = b.tag == "needle" ? Vec3(r * 2.2f, r * 3.2f, r * 2.2f) : Vec3(r * 3.0f, r * 2.4f, r * 3.0f);
		}
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		(void)c;
		const ff::BodyView& b = f.b;
		heat_ = b.mat == Mat::Glass ? Heat01(b) : 0.0f;
		if (sel.kind == ViewKind::CrystalWall) {
			pos_ = b.pos;
			basis_ = Basis::YawY(b.wall_yaw);
			size_ = Vec3(MaxF(b.wall_half.x, 0.4f), MaxF(b.wall_half.y * 2.0f, 0.4f), MaxF(b.wall_half.z * 2.0f, 0.3f));
			frost_ = (glass_ ? 0.05f : 0.45f) + 0.4f * b.wall_damage;
			rise_ = Sat(b.wall_rise);
			shatter_ = Sat(b.wall_damage * 1.4f - 0.2f);
		} else {
			pos_ = f.p;
			frost_ = glass_ ? 0.05f : 0.4f + 0.4f * (1.0f - b.liquid);
			rise_ = 1.0f;
			if (b.vel.length_squared() > 0.5f && b.controller < 0) basis_ = OrientAlong(b.vel, "lance");
		}
	}
	void Draw(Ctx& c, float fade) override {
		const CrystalLook& cl = glass_ ? c.cfg.glass : c.cfg.ice;
		Xform x;
		x.pos = pos_;
		x.basis = basis_.Scaled(size_.x, size_.y, size_.z);
		DrawItem& it = mode_ == CrystalMode::Shard
		                   ? c.out.AddStatic(key_, MatSlot::Crystal, MeshAsset::IceShard, x, &meshlib::Crystal(seed_, mode_))
		                   : c.out.Add(key_, MatSlot::Crystal, &meshlib::Crystal(seed_, mode_), x);
		it.params.Set(PV::Tint, Linear(cl.tint));
		it.params.Set(P::Opacity, cl.opacity);
		it.params.Set(P::Glow, cl.edge);
		it.params.Set(P::Heat, heat_);
		it.params.Set(P::Frost, Sat(frost_));
		it.params.Set(P::Rise, sel.kind == ViewKind::CrystalWall ? rise_ * fade : rise_);
		it.params.Set(P::RiseHeight, 1.15f);
		it.params.Set(P::Shatter, shatter_);
		it.params.Set(P::Fade, fade);
		it.params.Set(P::Seed, static_cast<float>(seed_ % 89u) * 0.1f);
	}
	float FadeTime() const override { return 0.2f; }

private:
	uint32_t key_ = 0, seed_ = 0;
	CrystalMode mode_ = CrystalMode::Shard;
	bool glass_ = false;
	Vec3 pos_, size_{0.3f, 0.3f, 0.3f};
	Basis basis_;
	float heat_ = 0.0f, frost_ = 0.35f, rise_ = 1.0f, shatter_ = 0.0f;
};

// --------------------------------------------------------------------------------------------- spikes
class SpikesView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		key_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
		const ff::BodyView& b = f.b;
		crystal_ = sel.style == "ice" || sel.style == "glass";
		if (b.form == Form::Wave) {
			layout_ = 2;
		} else if (b.form == Form::Zone) {
			layout_ = 1;
			Layout(c, Vec3(MaxF(b.zone_radius, 0.5f), 1.0f, 1.0f));
		} else {
			layout_ = 0;
			Layout(c, Vec3(MaxF(b.wall_half.x, 0.5f), MaxF(b.wall_half.y * 2.0f, 0.6f), 1.0f));
		}
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		if (layout_ == 2) {
			std::vector<Vec3> pts = PathWithHead(b, b.pos, true);
			const Vec3 sig(pts.empty() ? 0.0f : pts.back().x, pts.empty() ? 0.0f : pts.back().z, static_cast<float>(pts.size()));
			if (pts.size() >= 2 && sig != sig_) {
				sig_ = sig;
				PathLayout(c, pts, Clamp(0.6f + 0.2f * static_cast<float>(b.tier), 0.5f, 1.3f));
			}
			pos_ = Vec3();
			yaw_ = 0.0f;
			rise_ = 1.0f;
		} else {
			pos_ = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
			yaw_ = b.wall_yaw;
			rise_ = b.form == Form::Wall ? b.wall_rise : Life01(b);
		}
		heat_ = Heat01(b);
	}
	void Draw(Ctx& c, float fade) override {
		if (mesh_.Empty()) return;
		Xform x;
		x.pos = pos_;
		x.basis = Basis::YawY(yaw_);
		const MatSlot slot = crystal_ ? MatSlot::Crystal : (sel.style == "metal" ? MatSlot::Metal : MatSlot::Rock);
		DrawItem& it = c.out.Add(key_, slot, &mesh_, x);
		it.params.Set(P::Rise, Sat(rise_) * fade);
		it.params.Set(P::RiseHeight, 1.25f * height_);
		it.params.Set(P::Heat, heat_);
		it.params.Set(P::Detail, 1.6f);
		it.params.Set(P::Seed, SeedParam(seed_));
		it.params.Set(P::Fade, 1.0f);
		if (crystal_) {
			const CrystalLook& cl = sel.style == "glass" ? c.cfg.glass : c.cfg.ice;
			it.params.Set(PV::Tint, Linear(cl.tint));
			it.params.Set(P::Frost, cl.frost);
			it.params.Set(P::Opacity, cl.opacity + 0.15f);
			it.params.Set(P::Glow, cl.edge);
		}
		it.castShadow = !crystal_;
	}
	float FadeTime() const override { return 0.3f; }

private:
	void Put(Rng& r, const Vec3& p, float k, const Vec3& lean) {
		const float h = height_ * k * r.Range(0.7f, 1.05f);
		const float w = h * r.Range(0.16f, 0.24f);
		const Vec3 up = Norm(Vec3(0.0f, 1.0f, 0.0f) + lean + Vec3(r.Range(-0.15f, 0.15f), 0.0f, r.Range(-0.15f, 0.15f)));
		const Vec3 xx = Norm(up.cross(Vec3(0.0f, 0.0f, -1.0f)), Vec3(1.0f, 0.0f, 0.0f));
		const Vec3 zz = Norm(xx.cross(up));
		Basis b{xx * w, up * h, zz * w};
		Vec3 at = p;
		MeshData one;
		if (crystal_) {
			b = Basis{xx * (w * 5.0f), up * h, zz * (w * 5.0f)};   // shard mesh: radius 0.16, length 1 (centred)
			at = at + up * (h * 0.38f);
			one = meshlib::Crystal(seed_ + static_cast<uint32_t>(mesh_.NumVerts()), CrystalMode::Shard);
		} else {
			one = meshlib::Spike();
		}
		b = b.Mul(Basis::YawY(r.F01() * kFxTau));
		one.Transform({at, b});
		mesh_.Append(one);
	}
	void Layout(Ctx& c, const Vec3& size) {
		(void)c;
		height_ = MaxF(size.y, 0.2f);
		Rng r(static_cast<uint64_t>(seed_) * 271u + 9u);
		mesh_.Clear();
		if (layout_ == 0) {
			const int n = ClampI(static_cast<int>(size.x * 4.0f) + 3, 3, 16);
			for (int i = 0; i < n; ++i) {
				const float t = (static_cast<float>(i) + r.Range(-0.3f, 0.3f)) / static_cast<float>(MaxI(n - 1, 1));
				Put(r, Vec3(Lerp(-size.x, size.x, t), 0.0f, r.Range(-0.18f, 0.18f)),
				    1.0f - 0.4f * std::pow(std::fabs(t * 2.0f - 1.0f), 2.0f), Vec3());
			}
		} else {
			const int n = ClampI(static_cast<int>(size.x * 6.0f) + 4, 5, 16);
			for (int i = 0; i < n; ++i) {
				const float a = kFxTau * static_cast<float>(i) / static_cast<float>(n) + r.Range(-0.2f, 0.2f);
				const Vec3 d(std::cos(a), 0.0f, std::sin(a));
				Put(r, d * size.x, 1.0f, d * 0.35f);
			}
		}
		mesh_.Commit();
	}
	void PathLayout(Ctx& c, const std::vector<Vec3>& pts, float height) {
		(void)c;
		height_ = height;
		const int n = static_cast<int>(pts.size());
		float total = 0.0f;
		for (int i = 1; i < n; ++i) total += pts[static_cast<size_t>(i)].distance_to(pts[static_cast<size_t>(i - 1)]);
		const int k = ClampI(static_cast<int>(total / 0.5f) + 1, 2, 16);
		mesh_.Clear();
		for (int j = 0; j < k; ++j) {
			const float target = total * (static_cast<float>(j) + 0.5f) / static_cast<float>(k);
			float acc = 0.0f;
			Vec3 p = pts.back();
			for (int i = 1; i < n; ++i) {
				const float seg = pts[static_cast<size_t>(i)].distance_to(pts[static_cast<size_t>(i - 1)]);
				if (acc + seg >= target) {
					p = LerpV(pts[static_cast<size_t>(i - 1)], pts[static_cast<size_t>(i)], (target - acc) / MaxF(seg, 1e-4f));
					break;
				}
				acc += seg;
			}
			Rng r(static_cast<uint64_t>(j) * 977u + 3u);
			Put(r, p, Lerp(0.55f, 1.0f, static_cast<float>(j) / static_cast<float>(MaxI(k - 1, 1))), Vec3());
		}
		mesh_.Commit();
	}
	uint32_t key_ = 0, seed_ = 0;
	int layout_ = 0;   // 0 row, 1 ring, 2 path
	bool crystal_ = false;
	float height_ = 1.0f, yaw_ = 0.0f, rise_ = 1.0f, heat_ = 0.0f;
	Vec3 pos_, sig_{1e9f, 1e9f, 1e9f};
	MeshData mesh_;
};

// --------------------------------------------------------------------------------------------- vines
class VineView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		const std::string& st = sel.style;
		if (st == "lattice") vines_.Setup(c, "lattice", SeedOf(b.id), b.wall_half);
		else if (st == "briar") vines_.Setup(c, "briar", SeedOf(b.id), Vec3(1, 1, 1) * MaxF(b.zone_radius, 0.5f));
		else vines_.Setup(c, st, SeedOf(b.id), Vec3(1, 1, 1));
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		const std::string& st = sel.style;
		const float burn = Sat((b.temp - 120.0f) / 300.0f);
		const float frozen = b.phase == Phase::Frozen ? 1.0f : 0.0f;
		x_ = Xform();
		if (st == "lattice") {
			x_.pos = b.pos;
			x_.basis = Basis::YawY(b.wall_yaw);
			vines_.SetState(b.wall_rise, burn, frozen);
		} else if (st == "briar") {
			x_.pos = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
			vines_.SetState(Life01(b), burn, frozen);
		} else if (st == "roots") {
			std::vector<Vec3> pts(b.wave_path.begin(), b.wave_path.end());
			pts.push_back(b.pos);
			vines_.SetPath(pts, Clamp(0.06f + 0.02f * static_cast<float>(b.tier), 0.05f, 0.12f));
			vines_.SetState(1.0f, burn, frozen);
		} else {
			std::vector<Vec3> pts(trail.begin(), trail.end());
			pts.push_back(f.p);
			if (pts.size() >= 2) vines_.SetPath(pts, Clamp(b.radius * 0.35f, 0.04f, 0.1f));
			vines_.SetState(1.0f, burn, frozen);
		}
	}
	void Draw(Ctx& c, float fade) override { vines_.Draw(c, x_, fade); }
	float FadeTime() const override { return 0.35f; }

private:
	VineTubes vines_;
	Xform x_;
};

// --------------------------------------------------------------------------------------------- flames (field / line)
class FlamesView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		blue_ = b.props.get("blue", ff::Value(false)).truthy();
		const float tier = static_cast<float>(b.tier);
		if (sel.style == "field")
			flames_.Setup(c, FlameTongues::Mode::Field, SeedOf(b.id), MaxF(b.zone_radius, 0.5f), Clamp(0.75f + 0.15f * tier, 0.7f, 1.25f), blue_);
		else
			flames_.Setup(c, FlameTongues::Mode::Line, SeedOf(b.id), 0.6f, Clamp(0.8f + 0.15f * tier, 0.8f, 1.4f), blue_);
		for (uint32_t& k : loopKeys_) k = c.keys.New();
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		x_ = Xform();
		if (sel.style == "field") {
			x_.pos = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
			radius_ = MaxF(b.zone_radius, 0.5f);
			intensity_ = Life01(b) * Clamp(0.6f + b.power / 30.0f, 0.6f, 1.0f);
		} else {
			path_ = PathWithHead(b, b.pos, true);
			for (Vec3& p : path_) p.y = c.Ground(p);
			if (path_.size() >= 2) flames_.SetPath(c, path_);
			intensity_ = b.form == Form::Wave ? 1.0f : Clamp(b.heat_payload / 200.0f, 0.3f, 1.0f);
		}
	}
	void Draw(Ctx& c, float fade) override {
		// persistent Niagara fire on top (fx_config "niagara_loops"): sites on the field / along the line; the tongues
		// stay underneath, toned down when the slot replaces them. A fading view lets its systems burn out.
		const LCue cue = blue_ ? LCue::FireBlue : LCue::Fire;
		const bool loops = c.LoopOn(cue) && fade >= 1.0f && intensity_ > 0.05f;
		if (loops) {
			const std::array<Color, 4>& cols = blue_ ? c.cfg.blueFlame : c.cfg.flame;
			const Color smoke = blue_ ? Color(0.40f, 0.42f, 0.48f) : Color(0.42f, 0.40f, 0.38f);
			std::array<Vec3, kSites> sites{};
			const int n = Sites(sites);
			for (int k = 0; k < n; ++k) {
				LoopReq& l = c.out.Loop(loopKeys_[static_cast<size_t>(k)], cue, sites[static_cast<size_t>(k)]);
				l.scale = (sel.style == "field" ? Clamp(radius_ * 0.45f, 0.6f, 1.2f) : 0.75f) * (0.75f + 0.25f * intensity_);
				l.intensity = intensity_;
				l.color = cols[1];
				l.color2 = smoke;
			}
		}
		flames_.SetIntensity(intensity_ * (loops && c.cfg.Loop(cue).replace ? 0.55f : 1.0f));
		flames_.Draw(c, x_, fade, true);
	}
	float FadeTime() const override { return 0.35f; }

private:
	static constexpr int kSites = 4;   // GPU: each NS_Fire costs ~0.3-0.5 ms on the Mac
	// fire sites: a field's centre + a ring at 0.6 r; a line every ~1.8 m along its path
	int Sites(std::array<Vec3, kSites>& out) const {
		int n = 0;
		if (sel.style == "field") {
			out[static_cast<size_t>(n++)] = x_.pos;
			const int ring = ClampI(static_cast<int>(kFxTau * radius_ * 0.6f / 1.8f), 0, kSites - 2);
			for (int i = 0; i < ring; ++i) {
				const float a = kFxTau * (static_cast<float>(i) + 0.5f) / static_cast<float>(ring);
				out[static_cast<size_t>(n++)] = x_.pos + Vec3(std::cos(a), 0.0f, std::sin(a)) * (radius_ * 0.6f);
			}
			return n;
		}
		float carry = 0.9f;   // first site half a spacing in
		for (size_t i = 1; i < path_.size() && n < kSites; ++i) {
			const Vec3 a = path_[i - 1], b = path_[i];
			const float len = (b - a).length();
			float t = carry;
			while (t <= len && n < kSites) {
				out[static_cast<size_t>(n++)] = LerpV(a, b, t / MaxF(len, 1e-4f));
				t += 1.8f;
			}
			carry = t - len;
		}
		return n;
	}

	FlameTongues flames_;
	Xform x_;
	std::vector<Vec3> path_;
	std::array<uint32_t, kSites> loopKeys_{};
	float radius_ = 1.0f, intensity_ = 1.0f;
	bool blue_ = false;
};

// --------------------------------------------------------------------------------------------- fireballs
class FireballView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		keyTongues_ = c.keys.New();
		keyCore_ = c.keys.New();
		keyLight_ = c.keys.New();
		keyTrail_ = c.keys.New();
		kind_ = b.tag == "comet" ? 1 : (b.tag == "ember" ? 2 : 0);
		radius_ = kind_ == 2 ? 0.09f : Clamp(0.16f + 0.05f * static_cast<float>(b.tier) + b.heat_payload / 4000.0f, 0.16f, 0.55f);
		blue_ = b.props.get("blue", ff::Value(false)).truthy() || kind_ == 1;
		seed_ = std::fmod(radius_ * 13.7f, 1.0f);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		pos_ = f.p;
		Vec3 d = b.vel * -1.0f;
		if (d.length_squared() < 0.25f) d = Vec3(0.0f, 1.0f, 0.0f);
		const Vec3 want = Norm(LerpV(Norm(d), Vec3(0.0f, 1.0f, 0.0f), 0.25f));
		tail_ = Norm(LerpV(tail_, want, Sat(c.dt * 18.0f)), want);
		power_ = Clamp(0.6f + 0.15f * static_cast<float>(b.tier) + b.heat_payload / 1500.0f, 0.4f, 1.4f);
		scroll_ += c.dt * (kind_ == 1 ? 1.6f : 1.2f);
	}
	void Draw(Ctx& c, float fade) override {
		const float r = radius_ * (fade < 1.0f ? 0.4f + 0.6f * fade : 1.0f);
		const float len = r * (kind_ == 1 ? 5.5f : (kind_ == 2 ? 2.5f : 3.2f));
		const float wid = r * (kind_ == 1 ? 0.75f : (kind_ == 2 ? 0.9f : 1.05f));
		const float k = Lerp(0.8f, 1.15f, (power_ - 0.2f) / 1.3f);
		const Vec3 d = tail_;
		const Vec3 xax = Norm(d.cross(std::fabs(d.z) < 0.9f ? Vec3(0, 0, -1) : Vec3(1, 0, 0)));
		Xform xt;
		xt.pos = pos_ - d * (r * 0.35f);
		xt.basis = {xax * (wid * k), d * (len * k), Norm(xax.cross(d)) * (wid * k)};
		DrawItem& t = c.out.Add(keyTongues_, MatSlot::Flame, &meshlib::Flame(), xt);
		t.params.Set(P::Style, 1.0f);   // loop
		t.params.Set(P::Shape, 1.0f);   // teardrop
		t.params.Set(P::Cover, 0.85f);
		t.params.Set(P::Core, blue_ ? 0.35f : 0.0f);
		t.params.Set(P::Scroll, scroll_);
		t.params.Set(P::Intensity, (kind_ == 2 ? 1.6f : 1.5f) * fade);
		t.params.Set(P::Seed, seed_);
		const std::array<Color, 4>& cols = blue_ ? c.cfg.blueFlame : c.cfg.flame;
		t.params.Set(PV::Color, Linear(cols[0]));
		t.params.Set(PV::Color2, Linear(cols[1]));
		t.params.Set(PV::Color3, Linear(cols[2]));
		t.params.Set(PV::Color4, Linear(cols[3]));
		t.sortPriority = 2;
		Xform xc;
		xc.pos = pos_;
		xc.basis = Basis::Identity().Scaled(r * (kind_ == 1 ? 0.42f : 0.5f));
		DrawItem& core = c.out.Add(keyCore_, MatSlot::Shell, &meshlib::Sphere(1), xc);
		core.params.Set(P::Core, 1.0f);
		core.params.Set(P::Rim, 1.2f);
		core.params.Set(P::Opacity, 0.8f * fade);
		core.params.Set(P::Glow, 1.5f);
		core.params.Set(P::Cover, 0.3f);
		core.params.Set(PV::Color, Linear(cols[1]));
		core.params.Set(PV::Color2, Linear(cols[2]));
		core.params.Set(P::Phase, scroll_);
		core.sortPriority = 3;
		if (kind_ != 2)
			c.out.Light(keyLight_, pos_, blue_ ? Color(0.5f, 0.7f, 1.0f) : Color(1.0f, 0.55f, 0.2f),
			            (kind_ == 1 ? 1.2f : 1.5f) * power_ * (0.85f + 0.15f * std::sin(scroll_ * 17.0f)) * fade,
			            Clamp(radius_ * 10.0f, 2.0f, 6.0f), 2.0f);
		// a smoke / flare trail follows the ball (the system's particles stay in the world as it moves)
		const LCue cue = blue_ ? LCue::TrailBlue : LCue::TrailFire;
		if (kind_ != 2 && fade >= 1.0f && c.LoopOn(cue)) {
			LoopReq& l = c.out.Loop(keyTrail_, cue, pos_);
			l.dir = tail_;
			l.scale = Clamp(radius_ * 2.5f, 0.5f, 1.4f);
			l.intensity = power_;
			l.color = cols[1];
			l.color2 = blue_ ? Color(0.55f, 0.58f, 0.66f) : Color(0.56f, 0.54f, 0.52f);
		}
	}
	float FadeTime() const override { return 0.12f; }

private:
	uint32_t keyTongues_ = 0, keyCore_ = 0, keyLight_ = 0, keyTrail_ = 0;
	int kind_ = 0;   // 0 fireball, 1 comet, 2 ember
	float radius_ = 0.3f, power_ = 1.0f, scroll_ = 0.0f, seed_ = 0.0f;
	bool blue_ = false;
	Vec3 pos_, tail_{0.0f, 1.0f, 0.0f};
};

// --------------------------------------------------------------------------------------------- shells
class ShellView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		key_ = c.keys.New();
		keySpiral_ = c.keys.New();
		keyLight_ = c.keys.New();
		style_ = ShellStyle::Aura;
		for (int i = 0; i < kNumShellStyles; ++i)
			if (kShellStyleNames[static_cast<size_t>(i)] == sel.style) style_ = static_cast<ShellStyle>(i);
		blueMine_ = style_ == ShellStyle::Mine && b.mat == Mat::Fire && b.props.get("blue", ff::Value(false)).truthy();
		seed_ = SeedOf(b.id);
		arcRng_.Seed(static_cast<uint64_t>(seed_) * 97u + 3u);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		float r = b.zone_radius > 0.0f ? b.zone_radius : MaxF(b.radius, 0.15f);
		float hs = 1.0f;
		Vec3 at = f.p;
		switch (style_) {
			case ShellStyle::VacuumWell: hs = 0.45f; at = Vec3(f.p.x, c.Ground(f.p), f.p.z); break;
			case ShellStyle::StaticField: hs = 0.55f; at = Vec3(f.p.x, c.Ground(f.p), f.p.z); break;
			case ShellStyle::Inrush:
				hs = 0.5f;
				at = Vec3(f.p.x, c.Ground(f.p), f.p.z);
				if (b.max_life > 0.0f) r *= Lerp(1.0f, 0.35f, Sat(b.age / b.max_life));
				break;
			case ShellStyle::Corona:
			case ShellStyle::WindGuard:
			case ShellStyle::SoundBarrier:
				hs = 1.15f;
				at = b.form == Form::Zone ? f.p + Vec3(0.0f, 0.9f, 0.0f) : f.p;
				break;
			case ShellStyle::Mine: r = Clamp(b.radius, 0.1f, 0.25f); break;
			default: break;
		}
		radius_ = r;
		hs_ = hs;
		pos_ = at;
		power_ = b.form == Form::Zone ? Life01(b) * Clamp(0.5f + 0.17f * static_cast<float>(b.tier), 0.5f, 1.0f) : 1.0f;
		phase_ += c.dt;
		if (c.cfg.Shell(style_).arcs) {
			arcT_ -= c.dt;
			if (arcT_ <= 0.0f) {
				arcT_ = arcRng_.Range(0.12f, 0.3f);
				const float a = arcRng_.F01() * kFxTau;
				const float rr = radius_ * arcRng_.Range(0.3f, 0.9f);
				const Vec3 p0 = pos_ + Vec3(std::cos(a) * rr, 0.05f, std::sin(a) * rr);
				const float a2 = a + arcRng_.Range(0.6f, 1.6f);
				const Vec3 p1 = pos_ + Vec3(std::cos(a2) * rr * 0.8f, radius_ * hs_ * arcRng_.Range(0.2f, 0.6f), std::sin(a2) * rr * 0.8f);
				c.fx.Bolt(c, {p0, LerpV(p0, p1, 0.5f) + Vec3(0.0f, 0.2f, 0.0f), p1}, arcRng_.Next(), 0.6f, false, 0.05f);
			}
		}
	}
	void Draw(Ctx& c, float fade) override {
		const ShellLook& sl = c.cfg.Shell(style_);
		Color col = sl.color.a > 0.0f ? sl.color : c.cfg.MatColor(sl.fam);
		if (blueMine_) col = c.cfg.MatColor(Fam::Blue);
		const Color core = sl.coreColor.a > 0.0f ? sl.coreColor : col;
		Xform x;
		x.pos = pos_;
		x.basis = Basis::Identity().Scaled(radius_, radius_ * hs_, radius_);
		DrawItem& it = c.out.Add(key_, MatSlot::Shell, &meshlib::Sphere(2), x);
		it.params.Set(PV::Color, Linear(col));
		it.params.Set(PV::Color2, Linear(core));
		it.params.Set(P::Rim, sl.rim);
		it.params.Set(P::Streak, sl.streak);
		it.params.Set(P::Crackle, sl.crackle);
		it.params.Set(P::Core, sl.core);
		it.params.Set(P::Pulse, sl.pulse);
		it.params.Set(P::Opacity, sl.opacity * Lerp(0.55f, 1.25f, Sat(power_)) * fade);
		it.params.Set(P::Glow, sl.glow);
		it.params.Set(P::Absorb, sl.absorb);
		it.params.Set(P::Phase, phase_);
		it.params.Set(P::Seed, static_cast<float>(seed_ % 37u) * 0.31f);
		it.params.Set(P::Cover, 0.5f);
		it.params.Set(P::Height, hs_);
		it.sortPriority = 1;
		if (sl.spiral) {
			Xform xs;
			xs.pos = pos_ + Vec3(0.0f, 0.03f, 0.0f);
			xs.basis = Basis::Identity().Scaled(radius_ * 1.15f, 1.0f, radius_ * 1.15f);
			DrawItem& s = c.out.Add(keySpiral_, MatSlot::Ring, &meshlib::GroundQuad(), xs);
			s.params.Set(P::Style, 1.0f);
			s.params.Set(P::Cover, 0.6f);
			s.params.Set(PV::Color, Linear(LerpC(col, Color(0.1f, 0.05f, 0.2f), 0.35f)));
			s.params.Set(P::Phase, phase_);
			s.params.Set(P::Opacity, fade * Sat(power_ + 0.3f));
			s.params.Set(P::Glow, 1.0f);
			s.params.Set(P::Radius, 0.8f);
			s.params.Set(P::Width, 0.6f);
		}
		if (style_ == ShellStyle::Mine)
			c.out.Light(keyLight_, pos_, blueMine_ ? Color(0.5f, 0.7f, 1.0f) : Color(1.0f, 0.45f, 0.12f),
			            0.5f * (0.7f + 0.3f * std::sin(phase_ * 9.0f)) * fade, 2.5f, 0.8f);
	}
	float FadeTime() const override { return 0.3f; }

private:
	uint32_t key_ = 0, keySpiral_ = 0, keyLight_ = 0, seed_ = 0;
	ShellStyle style_ = ShellStyle::Aura;
	bool blueMine_ = false;
	Vec3 pos_;
	float radius_ = 1.0f, hs_ = 1.0f, power_ = 1.0f, phase_ = 0.0f, arcT_ = 0.0f;
	Rng arcRng_;
};

// --------------------------------------------------------------------------------------------- vortices
class VortexView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		keyOuter_ = c.keys.New();
		keyInner_ = c.keys.New();
		keyDebris_ = c.keys.New();
		seed_ = SeedOf(f.b.id);
		Rng r(4242u);
		for (int i = 0; i < kDebris; ++i)
			for (int k = 0; k < 4; ++k) deb_[i][k] = r.F01();
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		const std::string& st = sel.style;
		pos_ = Vec3(f.p.x, c.Ground(f.p), f.p.z);
		float vr = b.zone_radius > 0.0f ? b.zone_radius : MaxF(b.radius * 1.5f, 0.6f);
		float vh = Clamp(vr * 2.2f, 1.6f, 7.0f);
		if (st == "twister" || st == "spiral") {
			vr = Clamp(b.radius * 2.0f + 0.3f * static_cast<float>(b.tier), 0.45f, 1.2f);
			vh = Clamp(1.4f + 0.4f * static_cast<float>(b.tier), 1.2f, 3.0f);
		} else if (st == "eddy") {
			vh = Clamp(vr * 0.6f, 0.6f, 1.4f);
		} else if (st == "vortex_wall") {
			vh = 2.6f;
		}
		if (b.form == Form::Zone) vr *= 0.35f + 0.65f * Life01(b);
		radius_ = MaxF(vr, 0.2f);
		height_ = MaxF(vh, 0.3f);
		spin_ = std::fabs(b.spin) > 0.5f ? b.spin : 4.0f * (b.spin < 0.0f ? -1.0f : 1.0f);
		infusion_ = InfusionOf(b);
		spinPhase_ += spin_ / kFxTau * c.dt * 0.6f;
		risePhase_ += c.dt;
		// rebuild the funnels when the shape moved more than 2 %
		if (std::fabs(radius_ - builtR_) > 0.02f * builtR_ || std::fabs(height_ - builtH_) > 0.02f * builtH_) {
			BuildFunnels();
			builtR_ = radius_;
			builtH_ = height_;
		}
	}
	void Draw(Ctx& c, float fade) override {
		const VortexLook& vl = c.cfg.Vortex(infusion_);
		Xform x;
		x.pos = pos_;
		for (int layer = 0; layer < 2; ++layer) {
			const bool inner = layer == 1;
			DrawItem& it = c.out.Add(inner ? keyInner_ : keyOuter_, MatSlot::Vortex, inner ? &inner_ : &outer_, x);
			it.params.Set(PV::Color, Linear(vl.a));
			it.params.Set(PV::Color2, Linear(vl.b));
			it.params.Set(P::Opacity, vl.opacity * (inner ? 1.0f : 0.8f) * fade);
			it.params.Set(P::Cover, vl.cover);
			it.params.Set(P::Glow, vl.glow);
			it.params.Set(P::Seed, static_cast<float>(seed_ % 19u) + (inner ? 3.7f : 0.0f));
			it.params.Set(P::Phase, spinPhase_ * (inner ? 1.8f : 1.0f));
			it.params.Set(P::Scroll, risePhase_ * (inner ? 1.3f : 1.0f));
			it.params.Set(P::Height, height_);
			it.params.Set(P::Radius, radius_);
			it.sortPriority = inner ? 2 : 1;
		}
		const std::string& st = sel.style;
		if (!vl.debris || st == "eddy") return;
		// debris: inner chips orbit faster (angular speed ~ 1/r), climb and fall back on a slow cycle
		debris_.Clear();
		const int n = MinI(kDebris, c.q.debris);
		for (int i = 0; i < n; ++i) {
			const float s0 = deb_[i][0], s1 = deb_[i][1], s2 = deb_[i][2], s3 = deb_[i][3];
			const float hf = std::fmod(s1 + risePhase_ * (0.12f + 0.1f * s2), 1.0f);
			const float rr = Lerp(radius_ * 0.25f, radius_ * 0.85f, s2) * (0.5f + 0.6f * hf);
			const float ang = s0 * kFxTau + spinPhase_ * kFxTau * (1.4f / (0.4f + s2));
			const Vec3 p(std::cos(ang) * rr, hf * height_ * 0.85f + 0.1f, std::sin(ang) * rr);
			const float sz = Lerp(0.04f, 0.11f, s3) * (1.0f - Smooth(0.75f, 1.0f, hf)) * fade;
			if (sz < 0.004f) continue;
			MeshData one = meshlib::Chip();
			one.Transform({p, Basis::AxisAngle(Norm(Vec3(s1, s2, s3)), ang * 2.0f + s0 * 6.0f).Mul(Basis::Identity().Scaled(sz, sz * 0.7f, sz * 0.85f))});
			debris_.Append(one);
		}
		debris_.Commit();
		if (debris_.Empty()) return;
		DrawItem& d = c.out.Add(keyDebris_, MatSlot::Rock, &debris_, x);
		d.params.Set(P::Seed, 17.5f);
		d.params.Set(P::Rise, 1.0f);
		d.params.Set(P::Fade, 1.0f);
		if (infusion_ == Infusion::Sand) d.params.Set(PV::Tint, Color(2.4f, 1.9f, 1.3f));
	}
	float FadeTime() const override { return 0.45f; }

private:
	static constexpr int kDebris = 10;
	void BuildFunnels() {
		const std::string& st = sel.style;
		float rb = radius_ * 0.22f, sk = radius_ * 0.5f;
		if (st == "twister") {
			rb = radius_ * 0.3f;
			sk = radius_ * 0.25f;
		} else if (st == "eddy") {
			rb = radius_ * 0.55f;
			sk = radius_ * 0.2f;
		} else if (st == "funnel") {
			rb = radius_ * 0.4f;
			sk = radius_ * 0.3f;
		} else if (st == "vortex_wall") {
			rb = radius_ * 0.92f;
			sk = radius_ * 0.12f;
		}
		auto build = [](MeshData& m, float rBase, float rTop, float skirt, float h) {
			constexpr int rows = 16;
			float rad[rows], ys[rows];
			for (int i = 0; i < rows; ++i) {
				const float t = static_cast<float>(i) / static_cast<float>(rows - 1);
				rad[i] = Lerp(rBase, rTop, std::pow(t, 1.35f)) + skirt * std::pow(1.0f - t, 6.0f);
				ys[i] = t * h;
			}
			m.Clear();
			meshlib::BuildLathe(m, rad, ys, rows, 22, true);
			m.Commit();
		};
		build(outer_, rb, radius_, sk, height_);
		build(inner_, rb * 0.55f, radius_ * 0.62f, sk * 0.4f, height_ * 0.92f);
	}
	uint32_t keyOuter_ = 0, keyInner_ = 0, keyDebris_ = 0, seed_ = 0;
	float deb_[kDebris][4] = {};
	Vec3 pos_;
	float radius_ = 1.6f, height_ = 3.0f, spin_ = 4.0f, spinPhase_ = 0.0f, risePhase_ = 0.0f, builtR_ = -1.0f, builtH_ = -1.0f;
	Infusion infusion_ = Infusion::None;
	MeshData outer_, inner_, debris_;
};

// --------------------------------------------------------------------------------------------- wind blades
class BladeView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		key_ = c.keys.New();
		wall_ = sel.style == "wall";
		size_ = wall_ ? Vec3(MaxF(b.wall_half.x, b.radius), MaxF(b.wall_half.y * 2.0f, 1.8f), 1.0f)
		              : Vec3(1, 1, 1) * Clamp(b.radius * 1.6f, 0.5f, 1.4f);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		if (wall_) {
			pos_ = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
			basis_ = Basis::YawY(b.wall_yaw);
		} else {
			pos_ = f.p;
			if (b.vel.length_squared() >= 0.04f) {
				const Vec3 fw = Norm(b.vel);
				Vec3 x = Vec3(0.0f, 1.0f, 0.0f).cross(fw);
				x = x.length_squared() > 1e-6f ? Norm(x) : Vec3(1.0f, 0.0f, 0.0f);
				basis_ = {x, Norm(fw.cross(x)), fw};   // local +Z = travel, the arc lies in local XZ (a horizontal cut)
			}
		}
		power_ = Clamp(0.6f + 0.2f * static_cast<float>(b.tier), 0.5f, 1.3f);
		scroll_ += c.dt * (wall_ ? 1.1f : 2.2f);
	}
	void Draw(Ctx& c, float fade) override {
		Xform x;
		x.pos = pos_;
		x.basis = wall_ ? basis_.Scaled(MaxF(size_.x, 0.3f), MaxF(size_.y, 0.3f), 1.0f) : basis_.Scaled(MaxF(size_.x, 0.2f));
		DrawItem& it = c.out.Add(key_, MatSlot::Wind, wall_ ? &meshlib::Sheet() : &meshlib::Crescent(), x);
		it.params.Set(P::Style, 0.0f);   // crescent / sheet streaks
		it.params.Set(P::Dusty, wall_ ? 0.55f : 0.25f);
		it.params.Set(P::Opacity, (wall_ ? 0.55f : 1.0f) * Lerp(0.7f, 1.2f, Sat((power_ - 0.2f) / 1.3f)) * fade);
		it.params.Set(P::Cover, 0.5f);
		it.params.Set(P::Scroll, scroll_);
		it.params.Set(P::Age, 0.5f);
		it.params.Set(PV::Color, Linear(c.cfg.DustColor(Fam::Wind)));
		it.sortPriority = 1;
	}
	float FadeTime() const override { return 0.2f; }

private:
	uint32_t key_ = 0;
	bool wall_ = false;
	Vec3 pos_, size_{1, 1, 1};
	Basis basis_;
	float power_ = 1.0f, scroll_ = 0.0f;
};

// --------------------------------------------------------------------------------------------- ground current
class CrackleView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override { crackle_.Setup(c, Crackle::Mode::Ground, 0.5f, SeedOf(f.b.id)); }
	void Update(const BodyFrame& f, Ctx& c) override {
		(void)c;
		const ff::BodyView& b = f.b;
		std::vector<Vec3> pts(b.wave_path.begin(), b.wave_path.end());
		pts.push_back(b.pos);
		crackle_.SetTarget(b.pos, &pts);
		crackle_.SetIntensity(Clamp(0.5f + b.power / 30.0f, 0.4f, 1.3f));
	}
	void Draw(Ctx& c, float fade) override { crackle_.Draw(c, fade); }
	float FadeTime() const override { return 0.15f; }

private:
	Crackle crackle_;
};

// --------------------------------------------------------------------------------------------- ground decals (zones)
class DecalView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		key_ = c.keys.New();
		keyLight_ = c.keys.New();
		static const char* kStyles[] = {"quicksand", "ice_floor", "mud", "melt_pit", "static_field", "shade", "frost", "sand"};
		for (int i = 0; i < 8; ++i)
			if (sel.style == kStyles[i]) style_ = static_cast<float>(i);
		seed_ = static_cast<float>(SeedOf(b.id) % 41u) * 0.123f;
		pos_ = b.pos;
		radius_ = MaxF(b.zone_radius, 0.4f);
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		target_ = Life01(b);
		fade_ = MoveToward(fade_, target_, c.dt * 6.0f);
		phase_ += c.dt;
		heat_ = sel.style == "melt_pit" ? Clamp(0.4f + b.power / 30.0f, 0.4f, 1.0f) : 0.0f;
		pos_ = Vec3(b.pos.x, c.Ground(b.pos), b.pos.z);
	}
	void Draw(Ctx& c, float fade) override {
		Xform x;
		x.pos = pos_ + Vec3(0.0f, 0.012f, 0.0f);
		x.basis = Basis::Identity().Scaled(radius_, 1.0f, radius_);
		DrawItem& it = c.out.Add(key_, MatSlot::Ground, &meshlib::GroundQuad(), x);
		it.params.Set(P::Style, style_);
		it.params.Set(P::Fade, fade_ * fade);
		it.params.Set(P::Phase, phase_);
		it.params.Set(P::Heat, heat_);
		it.params.Set(P::Seed, seed_);
		it.sortPriority = -3;
		if (heat_ > 0.0f)
			c.out.Light(keyLight_, pos_ + Vec3(0.0f, 0.4f, 0.0f), Color(1.0f, 0.4f, 0.12f), heat_ * fade_ * fade, radius_ * 3.0f, 0.9f);
	}
	float FadeTime() const override { return 0.45f; }

private:
	static float MoveToward(float a, float b, float d) { return a < b ? MinF(a + d, b) : MaxF(a - d, b); }
	uint32_t key_ = 0, keyLight_ = 0;
	float style_ = 2.0f, seed_ = 0.0f, radius_ = 1.0f, fade_ = 0.0f, target_ = 1.0f, phase_ = 0.0f, heat_ = 0.0f;
	Vec3 pos_;
};

// --------------------------------------------------------------------------------------------- tremor / sound fronts
class RingsView final : public BodyView {
public:
	using BodyView::BodyView;
	void Init(const BodyFrame& f, Ctx& c) override {
		(void)f;
		(void)c;
	}
	void Update(const BodyFrame& f, Ctx& c) override {
		const ff::BodyView& b = f.b;
		ringT_ -= c.dt;
		dustT_ -= c.dt;
		if (ringT_ > 0.0f) return;
		// a sound flight field (under a hovering fighter) pulses slower and smaller than a travelling front
		const bool flight = b.tag == "flight_field";
		ringT_ = flight ? 0.3f : 0.12f;
		const Color col = c.cfg.MatColor(b.mat == Mat::Air ? Fam::Sound : Fam::Stone);
		const float gy = c.Ground(b.pos);
		RingOpts o;
		o.width = flight ? 0.08f : 0.1f;
		o.cover = flight ? 0.5f : 0.65f;
		o.glow = flight ? 1.2f : 1.0f;
		c.fx.Ring(c, Vec3(b.pos.x, gy + 0.04f, b.pos.z), Vec3(0.0f, 1.0f, 0.0f), flight ? 0.15f : 0.2f,
		          flight ? 0.9f : 1.4f + 0.3f * static_cast<float>(b.tier), flight ? 0.4f : 0.45f, col, o);
		if (b.mat == Mat::Stone && dustT_ <= 0.0f) {
			c.fx.Burst(c, Vec3(b.pos.x, gy, b.pos.z), Vec3(0.0f, 1.0f, 0.0f), 0.5f, Burst::Dust);
			dustT_ = 0.3f;
		}
	}
	void Draw(Ctx& c, float fade) override {
		(void)c;
		(void)fade;
	}
	float FadeTime() const override { return 0.0f; }

private:
	float ringT_ = 0.0f, dustT_ = 0.0f;
};

}  // namespace

std::unique_ptr<BodyView> MakeView(const ViewSel& sel) {
	switch (sel.kind) {
		case ViewKind::Stone: return std::make_unique<StoneView>(sel);
		case ViewKind::Wave: return std::make_unique<StripView>(sel, StripStyle::Lava);
		case ViewKind::LavaPool: return std::make_unique<StripView>(sel, StripStyle::Lava);
		case ViewKind::Strip: {
			StripStyle st = StripStyle::Water;
			if (sel.style == "sand") st = StripStyle::Sand;
			else if (sel.style == "rime") st = StripStyle::Rime;
			else if (sel.style == "mud") st = StripStyle::Mud;
			return std::make_unique<StripView>(sel, st);
		}
		case ViewKind::Wall: return std::make_unique<WallView>(sel);
		case ViewKind::Blob:
		case ViewKind::Ribbon:
		case ViewKind::Puddle: return std::make_unique<WaterView>(sel);
		case ViewKind::Metal: return std::make_unique<MetalView>(sel);
		case ViewKind::Cloud: return std::make_unique<CloudView>(sel);
		case ViewKind::Crystal:
		case ViewKind::CrystalWall: return std::make_unique<CrystalView>(sel);
		case ViewKind::Spikes: return std::make_unique<SpikesView>(sel);
		case ViewKind::Vine: return std::make_unique<VineView>(sel);
		case ViewKind::Flames: return std::make_unique<FlamesView>(sel);
		case ViewKind::Fireball: return std::make_unique<FireballView>(sel);
		case ViewKind::Shell: return std::make_unique<ShellView>(sel);
		case ViewKind::Vortex: return std::make_unique<VortexView>(sel);
		case ViewKind::Blade: return std::make_unique<BladeView>(sel);
		case ViewKind::Crackle: return std::make_unique<CrackleView>(sel);
		case ViewKind::Decal: return std::make_unique<DecalView>(sel);
		case ViewKind::Rings: return std::make_unique<RingsView>(sel);
		default: return nullptr;
	}
}

}  // namespace ffx
