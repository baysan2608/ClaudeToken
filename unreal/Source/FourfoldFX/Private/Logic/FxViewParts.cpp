// FourfoldFX logic island - reusable view parts: crackle, flame tongues, vine tubes, puff clouds. Owner: stream `fx`.
#include "FxMeshLib.h"
#include "FxViews.h"

#include <cmath>

namespace ffx {

// =============================================================================================== Crackle
void Crackle::Setup(Ctx& c, Mode m, float radius, uint32_t seed) {
	mode_ = m;
	radius_ = MaxF(radius, 0.05f);
	rng_.Seed(static_cast<uint64_t>(seed) * 7u + 1u);
	t_ = 0.0f;
	age_ = 1.0f;
	key_ = c.keys.New();
	lines_.clear();
}

void Crackle::SetTarget(const Vec3& center, const std::vector<Vec3>* path) {
	center_ = center;
	if (path) path_ = *path;
}

void Crackle::Draw(Ctx& c, float fade, int attachActor) {
	constexpr float kStrikeDur = 0.2f;
	age_ += c.dt;
	if (intensity_ > 0.02f && fade > 0.05f) {
		t_ -= c.dt;
		if (t_ <= 0.0f) {
			t_ = rng_.Range(0.06f, 0.18f) / MaxF(intensity_, 0.3f);
			std::vector<Vec3> pts;
			if (mode_ == Mode::Ground) {
				const int n = static_cast<int>(path_.size());
				if (n >= 2) {
					// crawl: a jagged arc from the front back along the path, hugging the ground
					const int i = n - 1 - rng_.RangeI(0, MinI(2, n - 2));
					const int j = MaxI(i - rng_.RangeI(2, 4), 0);
					const Vec3 a = path_[static_cast<size_t>(i)] + Vec3(0.0f, 0.05f, 0.0f);
					const Vec3 b = path_[static_cast<size_t>(j)] + Vec3(rng_.Range(-0.5f, 0.5f), 0.05f, rng_.Range(-0.5f, 0.5f));
					const Vec3 m = LerpV(a, b, 0.5f) + Vec3(rng_.Range(-0.3f, 0.3f), rng_.Range(0.1f, 0.35f), rng_.Range(-0.3f, 0.3f));
					pts = {a, m, b};
				}
			} else {
				const Vec3 d0 = Norm(Vec3(rng_.Signed(), rng_.Signed() * 0.7f, rng_.Signed()));
				const Vec3 d1 = Norm(d0 + rng_.InSphere() * 1.4f);
				const float r = radius_ * (mode_ == Mode::Body ? 1.05f : 0.55f);
				// actor mode: geometry relative to the attach bone (pelvis), else around the centre
				const Vec3 cc = attachActor >= 0 ? Vec3() : center_ + (mode_ == Mode::Actor ? Vec3(0.0f, 0.9f, 0.0f) : Vec3());
				const Vec3 hgt = mode_ == Mode::Actor ? Vec3(0.0f, rng_.Range(-0.6f, 0.6f), 0.0f) : Vec3();
				pts = {cc + d0 * r + hgt, cc + Norm(d0 + d1) * (r * 1.25f), cc + d1 * r - hgt * 0.5f};
			}
			if (pts.size() >= 2) {
				BoltParams bp;
				bp.maxLevels = MaxI(3, c.q.boltLevels - 2);
				bp.maxSegment = 0.12f;
				bp.width = mode_ == Mode::Ground ? 0.16f : 0.09f;
				bp.minBranches = 0;
				bp.maxBranches = 1;
				bp.branchLen = 0.3f;
				GenerateBolt(pts, rng_.Next(), bp, lines_);
				mesh_.Clear();
				BuildBoltMesh(mesh_, lines_, attachActor >= 0 ? c.in.cam.pos - center_ : c.in.cam.pos);
				mesh_.Commit();
				age_ = 0.0f;
			}
		}
	}
	if (age_ >= kStrikeDur || mesh_.Empty()) return;
	Xform x;
	if (attachActor >= 0) x.pos = center_;
	DrawItem& it = c.out.Add(key_, MatSlot::Lightning, &mesh_, x);
	it.params.Set(P::Age, age_ / kStrikeDur);
	it.params.Set(P::Intensity, Lerp(0.4f, 1.0f, MinF(intensity_, 1.0f)) * fade);
	it.params.Set(PV::Color, Linear(c.cfg.MatColor(Fam::Lightning)));
	it.sortPriority = 3;
	if (attachActor >= 0) {
		it.attachActor = attachActor;
		it.attachBone = Bone::Pelvis;
	}
}

// =============================================================================================== flame tongues
void FlameTongues::Setup(Ctx& c, Mode m, uint32_t seed, float radius, float height, bool blue) {
	mode_ = m;
	seed_ = seed;
	radius_ = MaxF(radius, 0.1f);
	height_ = MaxF(height, 0.1f);
	blue_ = blue;
	key_ = c.keys.New();
	keyLight_ = c.keys.New();
	sig_ = Vec3(1e9f, 1e9f, 1e9f);
	intensity_ = 1.0f;
	scroll_ = 0.0f;
	if (m == Mode::Line) {
		mesh_.Clear();
		mesh_.Commit();
		return;
	}
	Rng r(static_cast<uint64_t>(seed) * 613u + 11u);
	const int cap = c.q.flamesMax;
	const int n = m == Mode::Field ? ClampI(static_cast<int>(radius_ * radius_ * 2.2f) + 4, 4, cap) : (m == Mode::Burning ? 5 : 6);
	std::vector<Vec3> bases;
	std::vector<Color> data;
	for (int i = 0; i < n; ++i) {
		const float a = r.F01() * kFxTau;
		const float fi = static_cast<float>(i);
		const float rr = std::sqrt((fi + 0.5f) / static_cast<float>(n)) * radius_ * (m == Mode::Field ? 0.85f : 0.6f);
		Vec3 p(std::cos(a + fi * 2.4f) * rr, 0.0f, std::sin(a + fi * 2.4f) * rr);
		// burning: tongues around the body from the shins to the shoulders (relative to the pelvis bone)
		if (m == Mode::Burning) p.y = r.Range(0.2f, 1.1f) - 0.95f;
		const float hs = r.Range(0.6f, 1.1f) * (m == Mode::Field ? 1.0f - 0.35f * rr / MaxF(radius_, 0.01f) : 1.0f);
		bases.push_back(p);
		data.push_back(Color(r.F01(), hs, r.Range(0.8f, 1.2f), 1.0f));
	}
	lightPos_ = Vec3(0.0f, height_ * 0.6f, 0.0f);
	Build(c, bases, data);
}

void FlameTongues::SetPath(Ctx& c, const std::vector<Vec3>& pts) {
	const int n = static_cast<int>(pts.size());
	if (mode_ != Mode::Line || n < 2) return;
	const Vec3 sig(pts.back().x, pts.back().z, static_cast<float>(n));
	if (sig == sig_) return;
	sig_ = sig;
	float total = 0.0f;
	for (int i = 1; i < n; ++i) total += pts[static_cast<size_t>(i)].distance_to(pts[static_cast<size_t>(i - 1)]);
	const int k = ClampI(static_cast<int>(total / 0.45f) + 1, 2, c.q.flamesMax);
	std::vector<Vec3> bases;
	std::vector<Color> data;
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
		const float hs = Lerp(0.55f, 1.15f, static_cast<float>(j) / static_cast<float>(MaxI(k - 1, 1)));   // taller at the front
		bases.push_back(p);
		data.push_back(Color(std::fmod(static_cast<float>(j) * 0.618f, 1.0f), hs, 1.0f, 1.0f));
	}
	lightPos_ = pts.back() + Vec3(0.0f, height_ * 0.6f, 0.0f);
	Build(c, bases, data);
}

void FlameTongues::Build(Ctx& c, const std::vector<Vec3>& bases, const std::vector<Color>& data) {
	(void)c;
	// Each tongue: an 8 x 8 open teardrop at rest size; the material's vertex shader adds the lobes, flicker
	// and sway from uv0 (angle, y) + vertex colour (random, height scale, width scale, life).
	mesh_.Clear();
	const int sides = 8, rings = 8;
	const float fw = Clamp(height_ * 0.3f, 0.06f, 0.4f);
	for (size_t t = 0; t < bases.size(); ++t) {
		const Color& d = data[t];
		const float h = height_ * d.g, w = fw * d.b;
		const int b0 = mesh_.NumVerts();
		for (int i = 0; i < rings; ++i) {
			const float y = static_cast<float>(i) / static_cast<float>(rings - 1);
			const float tear = (std::sin(Sat(y * 1.1f) * kFxPi) * 0.9f + 0.1f * (1.0f - y)) * (1.0f - 0.3f * y);
			for (int k = 0; k <= sides; ++k) {
				const float u = static_cast<float>(k) / static_cast<float>(sides);
				const float a = u * kFxTau;
				const Vec3 dir(std::cos(a), 0.0f, -std::sin(a));
				mesh_.Add(bases[t] + dir * (tear * w) + Vec3(0.0f, y * h, 0.0f), dir, Vec2(u, y), d, Vec2(y, d.r),
				          Vec2(h, w), Vec3(-std::sin(a), 0.0f, -std::cos(a)));
			}
		}
		for (int i = 0; i + 1 < rings; ++i)
			for (int k = 0; k < sides; ++k) {
				const int a = b0 + i * (sides + 1) + k;
				const int b = a + sides + 1;
				mesh_.Quad(a, a + 1, b + 1, b);
			}
	}
	mesh_.Commit();
}

void FlameTongues::Draw(Ctx& c, const Xform& x, float fade, bool light, int attachActor, Bone bone) {
	scroll_ += c.dt;
	if (mesh_.Empty()) return;
	DrawItem& it = c.out.Add(key_, MatSlot::Flame, &mesh_, x);
	it.params.Set(P::Style, 2.0f);   // tongues (per-vertex rest shape + flicker)
	it.params.Set(P::Scroll, scroll_);
	it.params.Set(P::Intensity, intensity_ * fade);
	it.params.Set(P::Cover, blue_ ? 0.6f : 0.8f);
	it.params.Set(P::Height, height_);
	it.params.Set(P::Width, Clamp(height_ * 0.3f, 0.06f, 0.4f));
	const std::array<Color, 4>& cols = blue_ ? c.cfg.blueFlame : c.cfg.flame;
	it.params.Set(PV::Color, Linear(cols[0]));
	it.params.Set(PV::Color2, Linear(cols[1]));
	it.params.Set(PV::Color3, Linear(cols[2]));
	it.params.Set(PV::Color4, Linear(cols[3]));
	it.sortPriority = 2;
	if (attachActor >= 0) {
		it.attachActor = attachActor;
		it.attachBone = bone;
	}
	const float k = intensity_ * fade;
	if (light && k > 0.1f && mode_ != Mode::Burning) {
		const float flick = 0.85f + 0.15f * std::sin(scroll_ * 13.0f) * std::sin(scroll_ * 7.3f);
		c.out.Light(keyLight_, x.Apply(mode_ == Mode::Line ? lightPos_ - x.pos : lightPos_),
		            blue_ ? Color(0.45f, 0.65f, 1.0f) : Color(1.0f, 0.55f, 0.22f), 0.9f * k * flick,
		            Clamp(radius_ * 2.5f + 2.0f, 3.0f, 8.0f), 1.5f);
	}
}

// =============================================================================================== vines
void VineTubes::Setup(Ctx& c, const std::string& mode, uint32_t seed, const Vec3& size) {
	mode_ = mode;
	seed_ = seed;
	key_ = c.keys.New();
	paths_.clear();
	radii_.clear();
	sig_ = Vec3(1e9f, 1e9f, 1e9f);
	Rng r(static_cast<uint64_t>(seed) * 131u + 5u);
	if (mode == "lattice") {
		const float hw = MaxF(size.x, 0.3f), hh = MaxF(size.y, 0.3f) * 2.0f;
		for (int k = 0; k < 7; ++k) {
			const float fk = static_cast<float>(k);
			const float x0 = Lerp(-hw * 1.1f, hw * 1.1f, fk / 6.0f);
			const float dir = (k % 2 == 0) ? 1.0f : -1.0f;
			std::vector<Vec3> p;
			for (int j = 0; j < 9; ++j) {
				const float t = static_cast<float>(j) / 8.0f;
				p.push_back(Vec3(x0 + dir * t * hw * 0.9f + std::sin(t * 7.0f + fk) * 0.06f, t * hh, std::sin(t * 5.0f + fk * 1.3f) * 0.08f));
			}
			paths_.push_back(p);
			radii_.push_back(r.Range(0.045f, 0.07f));
		}
		for (int k = 0; k < 2; ++k) {
			const float fk = static_cast<float>(k);
			std::vector<Vec3> p;
			const float y = hh * (0.35f + 0.35f * fk);
			for (int j = 0; j < 9; ++j) {
				const float t = static_cast<float>(j) / 8.0f;
				p.push_back(Vec3(Lerp(-hw, hw, t), y + std::sin(t * 9.0f + fk) * 0.08f, 0.06f * std::sin(t * 4.0f)));
			}
			paths_.push_back(p);
			radii_.push_back(0.04f);
		}
	} else if (mode == "briar" || mode == "rooted") {
		const bool rooted = mode == "rooted";
		const float rad = MaxF(size.x, 0.2f);
		const int n = rooted ? 4 : 6;
		for (int k = 0; k < n; ++k) {
			const float a0 = kFxTau * static_cast<float>(k) / static_cast<float>(n) + r.Range(-0.3f, 0.3f);
			const float hgt = (rooted ? 0.95f : 0.7f) * r.Range(0.7f, 1.1f);
			std::vector<Vec3> p;
			for (int j = 0; j < 8; ++j) {
				const float t = static_cast<float>(j) / 7.0f;
				const float a = a0 + t * (rooted ? 2.2f : 1.2f);
				const float rr = rad * (1.0f - (rooted ? 0.45f : 0.1f) * t);
				p.push_back(Vec3(std::cos(a) * rr, -0.05f + t * hgt, std::sin(a) * rr));
			}
			paths_.push_back(p);
			radii_.push_back(r.Range(0.035f, 0.055f));
		}
	}
	dirty_ = true;
}

void VineTubes::SetPath(const std::vector<Vec3>& points, float radius) {
	const int n = static_cast<int>(points.size());
	if (n < 2) {
		paths_.clear();
		dirty_ = true;
		return;
	}
	const Vec3 sig(points.back().x + radius * 31.0f, points.back().z, static_cast<float>(n));
	if (sig == sig_) return;
	sig_ = sig;
	paths_.clear();
	radii_.clear();
	if (mode_ == "roots") {
		for (int s = 0; s < 3; ++s) {
			const float fs = static_cast<float>(s);
			const float off = (fs - 1.0f) * 0.35f;
			std::vector<Vec3> p;
			for (int i = 0; i < n; ++i) {
				const Vec3 q = points[static_cast<size_t>(i)];
				Vec3 t = points[static_cast<size_t>(MinI(i + 1, n - 1))] - points[static_cast<size_t>(MaxI(i - 1, 0))];
				const Vec3 side = t.length_squared() > 1e-8f ? Norm(Vec3(-t.z, 0.0f, t.x)) : Vec3(1.0f, 0.0f, 0.0f);
				const float fi = static_cast<float>(i);
				const float arch = std::fabs(std::sin(fi * 1.3f + fs)) * 0.35f * radius / 0.1f;
				p.push_back(q + side * (off + 0.08f * std::sin(fi * 2.1f + fs)) + Vec3(0.0f, arch - 0.05f, 0.0f));
			}
			paths_.push_back(p);
			radii_.push_back(radius * (1.0f - 0.25f * std::fabs(off)));
		}
	} else {
		paths_.push_back(points);
		radii_.push_back(radius);
	}
	dirty_ = true;
}

void VineTubes::SetState(float grow, float burn, float frozen) {
	grow_ = Sat(grow);
	burn_ = Sat(burn);
	frozen_ = Sat(frozen);
}

void VineTubes::Rebuild() {
	mesh_.Clear();
	for (size_t i = 0; i < paths_.size(); ++i) {
		const std::vector<Vec3>& p = paths_[i];
		const size_t n = p.size();
		if (n < 2) continue;
		// growth: the tube is drawn up to `grow` of its length (tip tapering to 30 %), the rest collapses
		const float shown = MaxF(grow_, 0.0f) * static_cast<float>(n - 1);
		const int last = MinI(static_cast<int>(std::ceil(shown)), static_cast<int>(n - 1));
		if (last < 1) continue;
		std::vector<Vec3> pts(p.begin(), p.begin() + last + 1);
		const float frac = shown - static_cast<float>(last - 1);
		pts.back() = LerpV(p[static_cast<size_t>(last - 1)], p[static_cast<size_t>(last)], Sat(frac));
		std::vector<float> rr(pts.size());
		for (size_t k = 0; k < pts.size(); ++k) {
			const float t = static_cast<float>(k) / static_cast<float>(n - 1);
			rr[k] = radii_[i] * Lerp(1.0f, 0.3f, t);
		}
		const int first = mesh_.NumVerts();
		AppendTube(mesh_, pts, rr, 6, false, true, 2);
		const float rnd = Hash01(seed_ * 31u + static_cast<uint32_t>(i));
		for (int v = first; v < mesh_.NumVerts(); ++v) mesh_.uv1[static_cast<size_t>(v)].y = rnd;
	}
	mesh_.Commit();
	builtGrow_ = grow_;
	dirty_ = false;
}

void VineTubes::Draw(Ctx& c, const Xform& x, float fade, int attachActor, Bone bone) {
	phase_ += c.dt;
	if (dirty_ || std::fabs(grow_ - builtGrow_) > 0.01f) Rebuild();
	if (mesh_.Empty()) return;
	Xform xx = x;
	if (fade < 1.0f) xx.basis = xx.basis.Scaled(1.0f, Lerp(0.2f, 1.0f, fade), 1.0f);   // wither down
	DrawItem& it = c.out.Add(key_, MatSlot::Vine, &mesh_, xx);
	it.params.Set(P::Burn, burn_);
	it.params.Set(P::Frozen, frozen_);
	it.params.Set(P::Phase, phase_);
	it.params.Set(P::Fade, fade);
	it.params.Set(P::Seed, static_cast<float>(seed_ % 97u) * 0.13f);
	it.castShadow = attachActor < 0;
	if (attachActor >= 0) {
		it.attachActor = attachActor;
		it.attachBone = bone;
	}
}

// =============================================================================================== puff clouds
void PuffCloud::Configure(Ctx& c, CloudStyle style, uint32_t seed) {
	style_ = style;
	keyOuter_ = c.keys.New();
	keyCore_ = c.keys.New();
	outer_.clear();
	core_.clear();
	auto layer = [](std::vector<Puff>& out, int n, uint32_t salt) {
		Rng r(7919u * salt);
		for (int i = 0; i < n; ++i) {
			Puff p;
			p.ang = r.F01();
			p.rf = std::sqrt(r.F01());
			p.h = r.F01();
			// stratified so a fraction `amount` of the puffs (rnd < amount) is spread evenly
			p.rnd = (static_cast<float>(i) + r.F01()) / static_cast<float>(n);
			out.push_back(p);
		}
	};
	layer(outer_, MaxI(c.q.cloudOuter, 3), 11u + seed % 5u);
	layer(core_, MaxI(c.q.cloudCore, 2), 29u + seed % 7u);
	phase_ = static_cast<float>(seed % 97u) * 0.37f;
}

void PuffCloud::Draw(Ctx& c, const Xform& x, float fade, float opacityScale, int attachActor, Bone bone) {
	phase_ += c.dt;
	const CloudLook& st = c.cfg.Cloud(style_);
	const bool slug = style_ == CloudStyle::Slug;
	const Vec3 camRight = Norm(c.in.cam.up.cross(c.in.cam.fwd * -1.0f), Vec3(1.0f, 0.0f, 0.0f));
	const Vec3 camUp = Norm(c.in.cam.up);
	const Vec3 worldUp(0.0f, 1.0f, 0.0f);
	for (int layerIdx = 0; layerIdx < 2; ++layerIdx) {
		const bool core = layerIdx == 1;
		const std::vector<Puff>& puffs = core ? core_ : outer_;
		MeshData& m = core ? meshCore_ : meshOuter_;
		m.Clear();
		const float radius = core ? radius_ * 0.6f : radius_;
		const float height = core ? height_ * 0.75f : height_;
		const float y0 = slug ? -height_ * (core ? 0.35f : 0.5f) : 0.0f;
		const float puffSize = st.size * (core ? 0.7f : 1.0f);
		const float swirl = st.swirl * (core ? 1.5f : 1.0f);
		const Color top = st.top;
		const Color low = core ? LerpC(st.low, st.top, 0.25f) : st.low;
		const float ph = phase_ * (core ? 1.07f : 1.0f);
		for (const Puff& pf : puffs) {
			float h = pf.h, life = 1.0f;
			if (st.rise > 0.0f) {
				h = std::fmod(pf.h + ph * st.rise * (0.75f + 0.5f * pf.rnd), 1.0f);
				life = Smooth(0.0f, 0.12f, h) * (1.0f - Smooth(0.62f, 1.0f, h));
			}
			const float a = pf.ang * kFxTau + ph * swirl * (1.6f - pf.rf);
			const float r = radius * pf.rf * Lerp(1.0f, 0.25f + 1.1f * h, st.column);
			Vec3 cl(std::cos(a) * r * squash_, y0 + h * height, std::sin(a) * r);
			cl += Vec3(std::sin(ph * 0.71f + pf.rnd * 21.0f), 0.35f * std::sin(ph * 0.93f + pf.rnd * 13.0f),
			           std::cos(ph * 0.63f + pf.rnd * 17.0f)) * st.wander;
			cl.z -= st.stretch * pf.rnd * 1.2f;
			const Vec3 centre = x.basis.Apply(cl);   // local to the item (the item sits at x.pos)
			const float size = puffSize * (0.65f + 0.7f * pf.rnd) * (1.0f + st.grow * h);
			const float rot = pf.rnd * 6.28f + ph * 0.25f * (pf.rnd - 0.5f);
			const float shown = pf.rnd <= amount_ * 1.02f ? 1.0f : 0.0f;
			const float alpha = life * shown * Smooth(0.0f, 0.15f, amount_) * fade;
			if (alpha <= 0.002f) continue;
			Vec3 up = Norm(LerpV(camUp, worldUp, 0.5f * st.flatten)) * (1.0f - 0.55f * st.flatten);
			const Vec3 hx = (camRight * std::cos(rot) + up * std::sin(rot)) * (size * 0.5f);
			const Vec3 hy = (up * std::cos(rot) - camRight * std::sin(rot)) * (size * 0.5f);
			const float tintK = Sat(h * 0.8f + 0.2f);
			const Color col = Linear(LerpC(low, top, tintK)).WithA(alpha);
			const int b = m.NumVerts();
			AppendQuad(m, centre, hx, hy, col);
			const float quadrant = std::floor(pf.rnd * 4.0f);
			for (int v = b; v < m.NumVerts(); ++v) {
				m.uv1[static_cast<size_t>(v)] = Vec2(quadrant, h);
				m.uv2[static_cast<size_t>(v)] = Vec2(h, pf.rnd);
				m.nrm[static_cast<size_t>(v)] = c.ToCam(x.pos + centre);
			}
		}
		m.Commit();
		if (m.Empty()) continue;
		Xform xi;
		xi.pos = x.pos;
		DrawItem& it = c.out.Add(core ? keyCore_ : keyOuter_, MatSlot::Smoke, &m, xi);
		it.params.flipbook = Flipbook::PuffAtlas;
		it.params.Set(PV::Color, Color(1, 1, 1, 1));
		it.params.Set(P::Opacity, (core ? st.opacityCore : st.opacity) * opacityScale);
		it.params.Set(P::Erosion, core ? 0.45f : 0.6f);
		it.params.Set(P::Phase, ph);
		it.params.Set(P::Style, 1.0f);   // persistent puffs: static atlas quadrant + noise erosion
		it.sortPriority = core ? 1 : 0;
		if (attachActor >= 0) {
			it.attachActor = attachActor;
			it.attachBone = bone;
		}
	}
}

}  // namespace ffx
