// FourfoldFX logic island - shared unit meshes (ports of VfxMesh / FxMesh). Owner: stream `fx`.
#include "FxMeshLib.h"

#include <cmath>
#include <memory>
#include <unordered_map>

namespace ffx {
namespace meshlib {

namespace {

MeshData& Finish(MeshData& m) {
	m.Commit();
	return m;
}

// Lathe (open) around +Y from a radius / height table. Smooth: normals from the profile slope; faceted: per face.
void LatheImpl(MeshData& m, const float* rad, const float* ys, int n, int sides, bool smooth) {
	if (n < 2 || sides < 3) return;
	if (smooth) {
		const int b0 = m.NumVerts();
		for (int i = 0; i < n; ++i) {
			const int ia = MaxI(i - 1, 0), ib = MinI(i + 1, n - 1);
			const float dr = rad[ib] - rad[ia], dy = ys[ib] - ys[ia];
			const float t = static_cast<float>(i) / static_cast<float>(n - 1);
			for (int k = 0; k <= sides; ++k) {
				const float u = static_cast<float>(k) / static_cast<float>(sides);
				const float a = u * kFxTau;
				// angle increases counter-clockwise seen from +Y: d = (cos a, 0, -sin a)
				const Vec3 d(std::cos(a), 0.0f, -std::sin(a));
				const Vec3 nn = Norm(d * dy - Vec3(0.0f, 1.0f, 0.0f) * dr, d);
				m.Add(Vec3(d.x * rad[i], ys[i], d.z * rad[i]), nn, Vec2(u, t), Color(), Vec2(t, 0.0f), Vec2(),
				      Vec3(-std::sin(a), 0.0f, -std::cos(a)));
			}
		}
		for (int i = 0; i + 1 < n; ++i)
			for (int k = 0; k < sides; ++k) {
				const int a = b0 + i * (sides + 1) + k;
				const int b = a + sides + 1;
				// (a+1 - a) x (b - a) ~ tangent(-sin, 0, -cos) x up = (cos?, ...) outward for ccw-from-top
				m.Quad(a, a + 1, b + 1, b);
			}
		return;
	}
	for (int i = 0; i + 1 < n; ++i) {
		const float t0 = static_cast<float>(i) / static_cast<float>(n - 1);
		const float t1 = static_cast<float>(i + 1) / static_cast<float>(n - 1);
		for (int k = 0; k < sides; ++k) {
			const float u0 = static_cast<float>(k) / static_cast<float>(sides);
			const float u1 = static_cast<float>(k + 1) / static_cast<float>(sides);
			const float a0 = u0 * kFxTau, a1 = u1 * kFxTau;
			const Vec3 d0(std::cos(a0), 0.0f, -std::sin(a0)), d1(std::cos(a1), 0.0f, -std::sin(a1));
			const Vec3 p00(d0.x * rad[i], ys[i], d0.z * rad[i]), p10(d1.x * rad[i], ys[i], d1.z * rad[i]);
			const Vec3 p01(d0.x * rad[i + 1], ys[i + 1], d0.z * rad[i + 1]), p11(d1.x * rad[i + 1], ys[i + 1], d1.z * rad[i + 1]);
			Vec3 fn = (p10 - p00).cross(p11 - p00);
			if (fn.length_squared() < 1e-14f) fn = (p11 - p00).cross(p01 - p00);
			fn = Norm(fn, Norm(d0 + d1));
			const Vec3 tg = Norm(p10 - p00, Vec3(1.0f, 0.0f, 0.0f));
			const int a = m.Add(p00, fn, Vec2(u0, t0), Color(), Vec2(t0, 0.0f), Vec2(), tg);
			const int b = m.Add(p10, fn, Vec2(u1, t0), Color(), Vec2(t0, 0.0f), Vec2(), tg);
			const int c = m.Add(p11, fn, Vec2(u1, t1), Color(), Vec2(t1, 0.0f), Vec2(), tg);
			const int d = m.Add(p01, fn, Vec2(u0, t1), Color(), Vec2(t1, 0.0f), Vec2(), tg);
			if (rad[i] > 1e-6f) m.Tri(a, b, c);
			if (rad[i + 1] > 1e-6f) m.Tri(a, c, d);
		}
	}
}

// One flat triangle facing away from `centre` (front by our winding rule).
void FlatTri(MeshData& m, const Vec3& a, const Vec3& b, const Vec3& c, const Vec2& ua, const Vec2& ub, const Vec2& uc,
             const Vec2& t1, const Vec3& centre, const Color& col = Color()) {
	Vec3 n = (b - a).cross(c - a);
	if (n.length_squared() < 1e-14f) return;
	n = Norm(n);
	const Vec3 tg = Norm(b - a, Perp(n));
	if (n.dot((a + b + c) / 3.0f - centre) >= 0.0f) {
		const int i0 = m.Add(a, n, ua, col, t1, Vec2(), tg);
		const int i1 = m.Add(b, n, ub, col, t1, Vec2(), tg);
		const int i2 = m.Add(c, n, uc, col, t1, Vec2(), tg);
		m.Tri(i0, i1, i2);
	} else {
		const Vec3 nn = n * -1.0f;
		const int i0 = m.Add(a, nn, ua, col, t1, Vec2(), tg);
		const int i1 = m.Add(c, nn, uc, col, t1, Vec2(), tg);
		const int i2 = m.Add(b, nn, ub, col, t1, Vec2(), tg);
		m.Tri(i0, i1, i2);
	}
}

// One hexagonal crystal (port of FxMesh._crystal).
void CrystalOne(MeshData& m, const Vec3& base, const Vec3& axisIn, float length, float radius, float rnd, bool twoTips,
                Rng& rng) {
	const int sides = 6;
	const Vec3 axis = Norm(axisIn);
	const Vec3 side = Norm(axis.cross(std::fabs(axis.y) > 0.9f ? Vec3(0.0f, 0.0f, -1.0f) : Vec3(0.0f, 1.0f, 0.0f)));
	const Vec3 fwd = Norm(axis.cross(side));
	const float body = length * rng.Range(0.62f, 0.74f);
	const float foot = twoTips ? length * 0.14f : 0.0f;
	const float rTop = radius * rng.Range(0.8f, 0.95f);
	Vec3 ringA[6], ringB[6];
	const float rot = rng.F01() * kFxTau;
	for (int k = 0; k < sides; ++k) {
		const float a = rot + kFxTau * static_cast<float>(k) / static_cast<float>(sides);
		const float wob = rng.Range(0.85f, 1.12f);
		const Vec3 d = (side * std::cos(a) + fwd * std::sin(a)) * wob;
		ringA[k] = base + axis * foot + d * radius;
		ringB[k] = base + axis * (foot + body) + d * rTop;
	}
	const Vec3 tip = base + axis * length + side * (rng.Range(-0.03f, 0.03f) * length);
	const Vec3 mid = base + axis * (foot + body * 0.5f);
	const Vec2 t1(rnd, base.y);
	for (int k = 0; k < sides; ++k) {
		const int k2 = (k + 1) % sides;
		FlatTri(m, ringA[k], ringA[k2], ringB[k2], Vec2(0, 0), Vec2(1, 0), Vec2(1, 1), t1, mid);
		FlatTri(m, ringA[k], ringB[k2], ringB[k], Vec2(0, 0), Vec2(1, 1), Vec2(0, 1), t1, mid);
		FlatTri(m, ringB[k], ringB[k2], tip, Vec2(0, 0.7f), Vec2(1, 0.7f), Vec2(0.5f, 1.0f), t1, mid);
		if (twoTips) FlatTri(m, ringA[k2], ringA[k], base, Vec2(0, 0.7f), Vec2(1, 0.7f), Vec2(0.5f, 1.0f), t1, mid);
	}
}

template <typename Key>
struct BoundedCache {
	std::unordered_map<Key, std::unique_ptr<MeshData>> map;
	size_t cap;
	explicit BoundedCache(size_t c) : cap(c) {}
	MeshData* Find(const Key& k) {
		auto it = map.find(k);
		return it == map.end() ? nullptr : it->second.get();
	}
	MeshData& Insert(const Key& k) {
		// Bounded: drop everything when full (meshes in use this frame are rebuilt next time they are asked for;
		// callers only hold the reference for the current frame).
		if (map.size() >= cap) map.clear();
		auto& slot = map[k];
		slot = std::make_unique<MeshData>();
		return *slot;
	}
};

}  // namespace

void BuildLathe(MeshData& m, const float* radius, const float* y, int n, int sides, bool smooth) {
	LatheImpl(m, radius, y, n, sides, smooth);
}

const MeshData& GroundQuad() {
	static MeshData m = [] {
		MeshData q;
		// front = hx x hy must be +Y: hx = +X, hy = -Z  (X x -Z = +Y)
		AppendQuad(q, Vec3(), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f));
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& FaceQuad() {
	static MeshData m = [] {
		MeshData q;
		AppendQuad(q, Vec3(), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f));
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Sphere(int detail) {
	static MeshData s[3];
	static bool built[3] = {false, false, false};
	const int d = ClampI(detail, 0, 2);
	if (!built[d]) {
		static const int kRings[3] = {8, 12, 16};
		static const int kSegs[3] = {12, 18, 24};
		AppendSphere(s[d], Vec3(), Vec3(1.0f, 1.0f, 1.0f), kRings[d], kSegs[d]);
		Finish(s[d]);
		built[d] = true;
	}
	return s[d];
}

const MeshData& Chip() {
	static MeshData m = [] {
		MeshData q;
		const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
		const Vec3 v[12] = {Vec3(-1, t, 0), Vec3(1, t, 0),  Vec3(-1, -t, 0), Vec3(1, -t, 0), Vec3(0, -1, t),  Vec3(0, 1, t),
		                    Vec3(0, -1, -t), Vec3(0, 1, -t), Vec3(t, 0, -1),  Vec3(t, 0, 1),  Vec3(-t, 0, -1), Vec3(-t, 0, 1)};
		static const int f[60] = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6,
		                          7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7,
		                          9, 8, 1};
		for (int i = 0; i < 60; i += 3)
			FlatTri(q, Norm(v[f[i]]), Norm(v[f[i + 1]]), Norm(v[f[i + 2]]), Vec2(0, 0), Vec2(1, 0), Vec2(0.5f, 1), Vec2(),
			        Vec3());
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Flame() {
	static MeshData m = [] {
		MeshData q;
		const int sides = 14, rings = 14;
		float rad[rings], ys[rings];
		for (int i = 0; i < rings; ++i) {
			rad[i] = 1.0f;
			ys[i] = static_cast<float>(i) / static_cast<float>(rings - 1);
		}
		LatheImpl(q, rad, ys, rings, sides, true);
		// cylinder normals are horizontal; the shader recomputes the silhouette softness from the view
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Cone() {
	static MeshData m = [] {
		MeshData q;
		const int rings = 10;
		float rad[rings], ys[rings];
		for (int i = 0; i < rings; ++i) {
			ys[i] = static_cast<float>(i) / static_cast<float>(rings - 1);
			rad[i] = 0.06f + 0.94f * ys[i];
		}
		LatheImpl(q, rad, ys, rings, 20, true);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Beam() {
	static MeshData m = [] {
		MeshData q;
		const int n = 17;
		for (int i = 0; i < n; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(n - 1);
			// strip in the XZ plane facing +Y (the item basis turns +Y toward the camera around the beam axis)
			q.Add(Vec3(-1.0f, 0.0f, t), Vec3(0.0f, 1.0f, 0.0f), Vec2(0.0f, t), Color(), Vec2(t, 0.0f), Vec2(),
			      Vec3(0.0f, 0.0f, 1.0f));
			q.Add(Vec3(1.0f, 0.0f, t), Vec3(0.0f, 1.0f, 0.0f), Vec2(1.0f, t), Color(), Vec2(t, 0.0f), Vec2(),
			      Vec3(0.0f, 0.0f, 1.0f));
		}
		for (int i = 0; i + 1 < n; ++i) {
			const int a = i * 2;
			// (right - left) x (next left - left) = X x Z = -Y: wind the other way so the front faces +Y
			q.Quad(a, a + 2, a + 3, a + 1);
		}
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Crescent() {
	static MeshData m = [] {
		MeshData q;
		// leading (outer) radius 1, arc of ~150 degrees in XZ, bulging to +Z, normal +Y
		AppendCrescent(q, Vec3(0.0f, 0.0f, -0.55f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 0.0f), 0.92f, 0.34f, 2.6f, 20);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Sheet() {
	static MeshData m = [] {
		MeshData q;
		const int nx = 12, ny = 6;
		for (int j = 0; j <= ny; ++j)
			for (int i = 0; i <= nx; ++i) {
				const float u = static_cast<float>(i) / static_cast<float>(nx);
				const float v = static_cast<float>(j) / static_cast<float>(ny);
				const float x = -1.0f + 2.0f * u;
				const float z = 0.15f * (1.0f - x * x);
				const Vec3 n = Norm(Vec3(0.3f * x, 0.0f, 1.0f));
				q.Add(Vec3(x, v, z), n, Vec2(u, 1.0f - v), Color(), Vec2(u, v), Vec2(), Vec3(1.0f, 0.0f, 0.0f));
			}
		for (int j = 0; j < ny; ++j)
			for (int i = 0; i < nx; ++i) {
				const int a = j * (nx + 1) + i;
				// (a+1 - a) x (a+row - a) = X x Y = +Z: front faces +Z
				q.Quad(a, a + 1, a + nx + 2, a + nx + 1);
			}
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Disc() {
	static MeshData m = [] {
		MeshData q;
		const float r[7] = {0.0f, 0.55f, 0.92f, 1.0f, 0.92f, 0.55f, 0.0f};
		const float y[7] = {-0.05f, -0.06f, -0.035f, 0.0f, 0.035f, 0.06f, 0.05f};
		LatheImpl(q, r, y, 7, 24, true);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Lance() {
	static MeshData m = [] {
		MeshData q;
		const float r[6] = {0.0f, 0.8f, 1.0f, 1.0f, 0.8f, 0.0f};
		const float y[6] = {-0.5f, -0.48f, -0.4f, 0.25f, 0.32f, 0.5f};
		LatheImpl(q, r, y, 6, 8, true);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Rod() {
	static MeshData m = [] {
		MeshData q;
		const float r[6] = {0.0f, 0.9f, 1.0f, 1.0f, 0.9f, 0.0f};
		const float y[6] = {-0.5f, -0.5f, -0.45f, 0.45f, 0.5f, 0.5f};
		LatheImpl(q, r, y, 6, 8, true);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Spike() {
	static MeshData m = [] {
		MeshData q;
		const float r[5] = {0.0f, 1.0f, 0.55f, 0.18f, 0.0f};
		const float y[5] = {-0.05f, 0.0f, 0.45f, 0.85f, 1.0f};
		LatheImpl(q, r, y, 5, 7, false);
		q.Commit();
		return q;
	}();
	return m;
}

const MeshData& Caltrop() {
	static MeshData m = [] {
		MeshData q;
		const Vec3 dirs[4] = {Vec3(0, 1, 0), Vec3(0.943f, -0.333f, 0), Vec3(-0.471f, -0.333f, 0.816f),
		                      Vec3(-0.471f, -0.333f, -0.816f)};
		for (const Vec3& d : dirs) {
			const Vec3 s = Norm(d.cross(std::fabs(d.y) < 0.9f ? Vec3(0, 0, -1) : Vec3(1, 0, 0)));
			const Vec3 t = Norm(d.cross(s));
			Vec3 ring[4];
			for (int k = 0; k < 4; ++k) {
				const float a = kFxTau * static_cast<float>(k) / 4.0f;
				ring[k] = (s * std::cos(a) + t * std::sin(a)) * 0.13f;
			}
			for (int k = 0; k < 4; ++k)
				FlatTri(q, ring[k], ring[(k + 1) % 4], d, Vec2(0, 0), Vec2(1, 0), Vec2(0.5f, 1), Vec2(), d * 0.3f);
		}
		q.Commit();
		return q;
	}();
	return m;
}

void BuildCrystal(MeshData& m, uint32_t seed, CrystalMode mode) {
	Rng rng(HashCombine(seed, 0xC4157A1u + static_cast<uint32_t>(mode) * 977u));
	switch (mode) {
		case CrystalMode::Shard:
			CrystalOne(m, Vec3(0.0f, -0.5f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 1.0f, 0.16f, rng.F01(), true, rng);
			break;
		case CrystalMode::Cluster: {
			const int n = rng.RangeI(5, 8);
			for (int i = 0; i < n; ++i) {
				const float ang = kFxTau * static_cast<float>(i) / static_cast<float>(n) + rng.Range(-0.3f, 0.3f);
				const float tilt = i > 0 ? rng.Range(0.15f, 0.55f) : 0.05f;
				const Vec3 axis = Norm(Vec3(std::cos(ang) * std::sin(tilt), std::cos(tilt), std::sin(ang) * std::sin(tilt)));
				const Vec3 base = Vec3(std::cos(ang), 0.0f, std::sin(ang)) * (rng.Range(0.0f, 0.25f) * (i > 0 ? 1.0f : 0.0f)) -
				                  Vec3(0.0f, 0.15f, 0.0f);
				const float ln = i > 0 ? rng.Range(0.55f, 1.0f) : 1.15f;
				CrystalOne(m, base, axis, ln, ln * rng.Range(0.12f, 0.18f), rng.F01(), false, rng);
			}
			break;
		}
		case CrystalMode::Wall:
		case CrystalMode::Ridge: {
			const bool wall = mode == CrystalMode::Wall;
			const int n = wall ? 13 : 11;
			const float hmax = wall ? 1.0f : 0.6f;
			for (int i = 0; i < n; ++i) {
				const float x = Lerp(-1.0f, 1.0f, (static_cast<float>(i) + rng.Range(-0.3f, 0.3f)) / static_cast<float>(n - 1));
				for (int row = 0; row < 2; ++row) {
					const float rowc = static_cast<float>(row) - 0.5f;
					const float z = rng.Range(-0.2f, 0.2f) + rowc * 0.22f;
					const Vec3 tilt = Norm(Vec3(rng.Range(-0.25f, 0.25f), 1.0f, rng.Range(-0.3f, 0.3f) + rowc * 0.4f));
					const float edge = 1.0f - 0.45f * std::pow(std::fabs(x), 3.0f);
					const float ln = hmax * edge * rng.Range(0.65f, 1.05f) * (row == 0 ? 1.0f : 0.8f);
					CrystalOne(m, Vec3(x, -0.12f, z), tilt, ln + 0.12f, rng.Range(0.13f, 0.2f) * (wall ? 1.2f : 1.0f),
					           rng.F01(), false, rng);
				}
			}
			break;
		}
	}
}

void BuildWall(MeshData& m, uint32_t seed) {
	Rng rng(HashCombine(seed, 0x1b873593u));
	const int blocks = 5;
	const float bw = 2.0f / static_cast<float>(blocks);
	for (int bi = 0; bi < blocks; ++bi) {
		const float cx = -1.0f + bw * (static_cast<float>(bi) + 0.5f);
		const float edge = std::fabs(static_cast<float>(bi) - (blocks - 1) * 0.5f) / ((blocks - 1) * 0.5f);
		const float h = (1.0f - 0.2f * edge) * rng.Range(0.93f, 1.0f);
		const float hw = bw * 0.54f + rng.Range(-0.01f, 0.01f);
		const float hd = rng.Range(0.21f, 0.26f);
		const float ch = MinF(hw, hd) * rng.Range(0.38f, 0.5f);
		const float yaw = rng.Range(-0.07f, 0.07f);
		const Color col(edge, rng.F01(), 0.0f, 1.0f);   // r = rise delay, g = block random
		const Vec2 plan[8] = {Vec2(hw - ch, -hd), Vec2(hw, -hd + ch), Vec2(hw, hd - ch), Vec2(hw - ch, hd),
		                      Vec2(-hw + ch, hd), Vec2(-hw, hd - ch), Vec2(-hw, -hd + ch), Vec2(-hw + ch, -hd)};
		Vec3 rb[8], rs[8], rt[8];
		for (int i = 0; i < 8; ++i) {
			const Vec2 p = plan[i];
			const Vec2 q(p.x * std::cos(yaw) - p.y * std::sin(yaw), p.x * std::sin(yaw) + p.y * std::cos(yaw));
			const Vec3 jit(rng.Signed() * 0.012f, rng.Signed() * 0.015f, rng.Signed() * 0.012f);
			rb[i] = Vec3(cx + q.x * 1.05f, 0.0f, q.y * 1.05f);
			rs[i] = Vec3(cx + q.x + jit.x, h * 0.84f + jit.y * 1.5f, q.y + jit.z);
			rt[i] = Vec3(cx + q.x * 0.78f + jit.x, h * (1.0f + rng.Range(-0.03f, 0.01f)), q.y * 0.74f + jit.z);
		}
		const Vec3 top(cx + rng.Range(-0.02f, 0.02f), h * 1.02f, rng.Range(-0.02f, 0.02f));
		const Vec3 body(cx, h * 0.5f, 0.0f);
		for (int i = 0; i < 8; ++i) {
			const int j = (i + 1) % 8;
			// uv0: planar per side (metres), so the rock pattern scale is the same on every block
			auto uvs = [](const Vec3& p) { return Vec2(p.x + p.z, p.y); };
			FlatTri(m, rb[i], rb[j], rs[j], uvs(rb[i]), uvs(rb[j]), uvs(rs[j]), Vec2(), body, col);
			FlatTri(m, rb[i], rs[j], rs[i], uvs(rb[i]), uvs(rs[j]), uvs(rs[i]), Vec2(), body, col);
			FlatTri(m, rs[i], rs[j], rt[j], uvs(rs[i]), uvs(rs[j]), uvs(rt[j]), Vec2(), body, col);
			FlatTri(m, rs[i], rt[j], rt[i], uvs(rs[i]), uvs(rt[j]), uvs(rt[i]), Vec2(), body, col);
			FlatTri(m, rt[i], rt[j], top, Vec2(rt[i].x, rt[i].z), Vec2(rt[j].x, rt[j].z), Vec2(top.x, top.z), Vec2(), body, col);
		}
	}
}

const MeshData& Crystal(uint32_t seed, CrystalMode mode) {
	static BoundedCache<uint32_t> cache(64);
	const uint32_t key = (seed % 16u) * 4u + static_cast<uint32_t>(mode);
	if (MeshData* hit = cache.Find(key)) return *hit;
	MeshData& m = cache.Insert(key);
	BuildCrystal(m, seed % 16u, mode);
	return Finish(m);
}

const MeshData& Wall(uint32_t seed) {
	static BoundedCache<uint32_t> cache(16);
	const uint32_t key = seed % 16u;
	if (MeshData* hit = cache.Find(key)) return *hit;
	MeshData& m = cache.Insert(key);
	BuildWall(m, key);
	return Finish(m);
}

const MeshData& Rock(uint32_t seed) {
	static BoundedCache<uint32_t> cache(64);
	const uint32_t key = seed % 64u;   // bounded key space: the cache never clears
	if (MeshData* hit = cache.Find(key)) return *hit;
	MeshData& m = cache.Insert(key);
	AppendRock(m, key * 7919u + 13u, 1.0f);
	return Finish(m);
}

}  // namespace meshlib
}  // namespace ffx
