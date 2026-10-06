// FourfoldFX logic island - mesh buffers and procedural builders. Owner: stream `fx`.
#include "FxMesh.h"

#include <algorithm>
#include <cmath>

namespace ffx {

void MeshData::Clear() {
	pos.clear();
	nrm.clear();
	tan.clear();
	uv0.clear();
	uv1.clear();
	uv2.clear();
	col.clear();
	idx.clear();
}

int MeshData::Add(const Vec3& p, const Vec3& n, const Vec2& t0, const Color& c, const Vec2& t1, const Vec2& t2,
                  const Vec3& tg) {
	pos.push_back(p);
	nrm.push_back(n);
	tan.push_back(tg);
	uv0.push_back(t0);
	uv1.push_back(t1);
	uv2.push_back(t2);
	col.push_back(c);
	return static_cast<int>(pos.size()) - 1;
}

void MeshData::Tri(int a, int b, int c) {
	idx.push_back(a);
	idx.push_back(b);
	idx.push_back(c);
}

void MeshData::Quad(int a, int b, int c, int d) {
	Tri(a, b, c);
	Tri(a, c, d);
}

void MeshData::Append(const MeshData& o) {
	const int base = NumVerts();
	pos.insert(pos.end(), o.pos.begin(), o.pos.end());
	nrm.insert(nrm.end(), o.nrm.begin(), o.nrm.end());
	tan.insert(tan.end(), o.tan.begin(), o.tan.end());
	uv0.insert(uv0.end(), o.uv0.begin(), o.uv0.end());
	uv1.insert(uv1.end(), o.uv1.begin(), o.uv1.end());
	uv2.insert(uv2.end(), o.uv2.begin(), o.uv2.end());
	col.insert(col.end(), o.col.begin(), o.col.end());
	for (int32_t i : o.idx) idx.push_back(i + base);
}

void MeshData::ComputeNormals() {
	std::vector<Vec3> acc(pos.size(), Vec3());
	for (size_t i = 0; i + 2 < idx.size(); i += 3) {
		const size_t a = static_cast<size_t>(idx[i]), b = static_cast<size_t>(idx[i + 1]), c = static_cast<size_t>(idx[i + 2]);
		const Vec3 fn = (pos[b] - pos[a]).cross(pos[c] - pos[a]);
		acc[a] += fn;
		acc[b] += fn;
		acc[c] += fn;
	}
	for (size_t i = 0; i < pos.size(); ++i) nrm[i] = Norm(acc[i], nrm[i].length_squared() > 0.0f ? nrm[i] : Vec3(0.0f, 1.0f, 0.0f));
}

void MeshData::ComputeTangents() {
	std::vector<Vec3> acc(pos.size(), Vec3());
	for (size_t i = 0; i + 2 < idx.size(); i += 3) {
		const size_t a = static_cast<size_t>(idx[i]), b = static_cast<size_t>(idx[i + 1]), c = static_cast<size_t>(idx[i + 2]);
		const Vec3 e1 = pos[b] - pos[a], e2 = pos[c] - pos[a];
		const float du1 = uv0[b].x - uv0[a].x, dv1 = uv0[b].y - uv0[a].y;
		const float du2 = uv0[c].x - uv0[a].x, dv2 = uv0[c].y - uv0[a].y;
		const float det = du1 * dv2 - du2 * dv1;
		if (std::fabs(det) < 1e-12f) continue;
		const Vec3 t = (e1 * dv2 - e2 * dv1) / det;
		acc[a] += t;
		acc[b] += t;
		acc[c] += t;
	}
	for (size_t i = 0; i < pos.size(); ++i) {
		const Vec3 n = nrm[i];
		Vec3 t = acc[i] - n * acc[i].dot(n);
		tan[i] = Norm(t, Perp(n));
	}
}

void MeshData::Transform(const Xform& x) {
	for (Vec3& p : pos) p = x.Apply(p);
	// normals: inverse transpose; for orthogonal-with-scale bases dividing by squared scale per axis is exact
	const float sx = x.basis.x.length_squared(), sy = x.basis.y.length_squared(), sz = x.basis.z.length_squared();
	const Basis inv{x.basis.x / MaxF(sx, 1e-12f), x.basis.y / MaxF(sy, 1e-12f), x.basis.z / MaxF(sz, 1e-12f)};
	for (Vec3& n : nrm) n = Norm(inv.Apply(n));
	for (Vec3& t : tan) t = Norm(x.basis.Apply(t), Vec3(1.0f, 0.0f, 0.0f));
}

void MeshData::PadTo(int vertCapacity, int idxCapacity) {
	if (pos.empty()) Add(Vec3(), Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
	const Vec3 p0 = pos[0];
	while (NumVerts() < vertCapacity) Add(p0, Vec3(0.0f, 1.0f, 0.0f), Vec2(), Color(0.0f, 0.0f, 0.0f, 0.0f));
	while (static_cast<int>(idx.size()) + 3 <= idxCapacity) Tri(0, 0, 0);
}

void MeshData::Commit() {
	uint64_t h = 1469598103934665603ULL ^ static_cast<uint64_t>(pos.size());
	for (int32_t i : idx) {
		h ^= static_cast<uint64_t>(static_cast<uint32_t>(i));
		h *= 1099511628211ULL;
	}
	h ^= static_cast<uint64_t>(idx.size()) << 32;
	if (!committed_ || h != lastTopoHash_) ++topo;
	lastTopoHash_ = h;
	committed_ = true;
	++version;
}

bool MeshData::Valid() const {
	const size_t n = pos.size();
	if (nrm.size() != n || tan.size() != n || uv0.size() != n || uv1.size() != n || uv2.size() != n || col.size() != n)
		return false;
	if (idx.size() % 3 != 0) return false;
	for (int32_t i : idx)
		if (i < 0 || static_cast<size_t>(i) >= n) return false;
	return true;
}

// ------------------------------------------------------------------------------------------------ builders

void AppendQuad(MeshData& m, const Vec3& c, const Vec3& hx, const Vec3& hy, const Color& col, const Vec2& uvMin,
                const Vec2& uvMax) {
	const Vec3 n = Norm(hx.cross(hy));
	const Vec3 t = Norm(hx);
	const int a = m.Add(c - hx - hy, n, Vec2(uvMin.x, uvMax.y), col, Vec2(), Vec2(), t);
	const int b = m.Add(c + hx - hy, n, Vec2(uvMax.x, uvMax.y), col, Vec2(), Vec2(), t);
	const int d = m.Add(c + hx + hy, n, Vec2(uvMax.x, uvMin.y), col, Vec2(), Vec2(), t);
	const int e = m.Add(c - hx + hy, n, Vec2(uvMin.x, uvMin.y), col, Vec2(), Vec2(), t);
	m.Quad(a, b, d, e);
}

static float PickF(const std::vector<float>& v, size_t i, float def) {
	if (v.empty()) return def;
	return v.size() == 1 ? v[0] : v[std::min(i, v.size() - 1)];
}
static Color PickC(const std::vector<Color>& v, size_t i) {
	if (v.empty()) return Color();
	return v.size() == 1 ? v[0] : v[std::min(i, v.size() - 1)];
}

void AppendRibbon(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& w, const Vec3& camPos,
                  const std::vector<Color>& cols, float strand) {
	const size_t n = pts.size();
	if (n < 2) return;
	float total = 0.0f;
	for (size_t i = 1; i < n; ++i) total += pts[i].distance_to(pts[i - 1]);
	const int base = m.NumVerts();
	float s = 0.0f;
	Vec3 lastSide(0.0f, 1.0f, 0.0f);
	for (size_t i = 0; i < n; ++i) {
		if (i > 0) s += pts[i].distance_to(pts[i - 1]);
		const Vec3 t = Norm(pts[std::min(i + 1, n - 1)] - pts[i > 0 ? i - 1 : 0], Vec3(1.0f, 0.0f, 0.0f));
		const Vec3 toCam = Norm(camPos - pts[i], Vec3(0.0f, 0.0f, 1.0f));
		Vec3 side = t.cross(toCam);
		side = side.length_squared() > 1e-10f ? Norm(side) : lastSide;
		if (i > 0 && side.dot(lastSide) < 0.0f) side = side * -1.0f;
		lastSide = side;
		const float hw = 0.5f * PickF(w, i, 0.1f);
		const Color c = PickC(cols, i);
		const float along = total > 0.0f ? s / total : 0.0f;
		const Vec3 n = Norm(side.cross(t), toCam);   // faces the camera
		m.Add(pts[i] - side * hw, n, Vec2(0.0f, s), c, Vec2(along, strand), Vec2(), t);
		m.Add(pts[i] + side * hw, n, Vec2(1.0f, s), c, Vec2(along, strand), Vec2(), t);
	}
	for (size_t i = 0; i + 1 < n; ++i) {
		const int a = base + static_cast<int>(i) * 2;
		// a = left(i), a+1 = right(i), a+2 = left(i+1), a+3 = right(i+1); front toward the camera:
		// cross(right - left, next - left) = side x t (* hw * ds) which equals n's direction.
		m.Quad(a, a + 1, a + 3, a + 2);
	}
}

void AppendFlatRibbon(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& w, const Vec3& up,
                      const Color& col) {
	const size_t n = pts.size();
	if (n < 2) return;
	float total = 0.0f;
	for (size_t i = 1; i < n; ++i) total += pts[i].distance_to(pts[i - 1]);
	const int base = m.NumVerts();
	float s = 0.0f;
	const Vec3 nu = Norm(up);
	for (size_t i = 0; i < n; ++i) {
		if (i > 0) s += pts[i].distance_to(pts[i - 1]);
		const Vec3 t = Norm(pts[std::min(i + 1, n - 1)] - pts[i > 0 ? i - 1 : 0], Vec3(1.0f, 0.0f, 0.0f));
		const Vec3 side = Norm(t.cross(nu), Perp(nu));
		const float hw = 0.5f * PickF(w, i, 0.1f);
		const float along = total > 0.0f ? s / total : 0.0f;
		m.Add(pts[i] - side * hw, nu, Vec2(0.0f, s), col, Vec2(along, 0.0f), Vec2(), t);
		m.Add(pts[i] + side * hw, nu, Vec2(1.0f, s), col, Vec2(along, 0.0f), Vec2(), t);
	}
	for (size_t i = 0; i + 1 < n; ++i) {
		const int a = base + static_cast<int>(i) * 2;
		// side = t x up, so cross(side, t) = up: front faces up.
		m.Quad(a, a + 1, a + 3, a + 2);
	}
}

void AppendTube(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& r, int sides, bool capStart,
                bool capEnd, int capRings, const Color& col) {
	const size_t n = pts.size();
	if (n < 2 || sides < 3) return;
	// rotation-minimising frames (double reflection, Wang et al. 2008)
	std::vector<Vec3> T(n), U(n);
	for (size_t i = 0; i < n; ++i) T[i] = Norm(pts[std::min(i + 1, n - 1)] - pts[i > 0 ? i - 1 : 0], Vec3(0.0f, 1.0f, 0.0f));
	U[0] = Perp(T[0]);
	for (size_t i = 0; i + 1 < n; ++i) {
		const Vec3 v1 = pts[i + 1] - pts[i];
		const float c1 = v1.dot(v1);
		if (c1 < 1e-12f) {
			U[i + 1] = U[i];
			continue;
		}
		const Vec3 rL = U[i] - v1 * (2.0f / c1 * v1.dot(U[i]));
		const Vec3 tL = T[i] - v1 * (2.0f / c1 * v1.dot(T[i]));
		const Vec3 v2 = T[i + 1] - tL;
		const float c2 = v2.dot(v2);
		U[i + 1] = c2 < 1e-12f ? rL : rL - v2 * (2.0f / c2 * v2.dot(rL));
		U[i + 1] = Norm(U[i + 1] - T[i + 1] * U[i + 1].dot(T[i + 1]), Perp(T[i + 1]));
	}
	float total = 0.0f;
	std::vector<float> sAt(n, 0.0f);
	for (size_t i = 1; i < n; ++i) {
		total += pts[i].distance_to(pts[i - 1]);
		sAt[i] = total;
	}
	auto ringAt = [&](const Vec3& c, const Vec3& t, const Vec3& u, float rad, float s, float along, float nTilt) {
		const Vec3 v = t.cross(u);
		const int first = m.NumVerts();
		for (int k = 0; k <= sides; ++k) {
			const float a = kFxTau * static_cast<float>(k) / static_cast<float>(sides);
			const Vec3 d = u * std::cos(a) + v * std::sin(a);
			const Vec3 nn = Norm(d * std::sqrt(MaxF(0.0f, 1.0f - nTilt * nTilt)) + t * nTilt, d);
			m.Add(c + d * rad, nn, Vec2(static_cast<float>(k) / static_cast<float>(sides), s), col, Vec2(along, 0.0f),
			      Vec2(), t);
		}
		return first;
	};
	auto stitch = [&](int r0, int r1) {
		for (int k = 0; k < sides; ++k) {
			// ring vertices go counter-clockwise around t (d = u cos + v sin, v = t x u); outward front:
			// cross((r0,k+1) - (r0,k), (r1,k) - (r0,k)) ~ (dir of increasing angle) x t = outward.
			m.Quad(r0 + k, r0 + k + 1, r1 + k + 1, r1 + k);
		}
	};
	int prev = -1;
	if (capStart) {
		const float rad = PickF(r, 0, 0.05f);
		for (int j = 0; j < capRings; ++j) {
			const float phi = (kFxPi * 0.5f) * (1.0f - static_cast<float>(j) / static_cast<float>(capRings));
			const float cr = rad * std::cos(phi);
			const Vec3 c = pts[0] - T[0] * (rad * std::sin(phi));
			const int ring = ringAt(c, T[0], U[0], MaxF(cr, rad * 0.02f), -rad * std::sin(phi), 0.0f, -std::sin(phi));
			if (prev >= 0) stitch(prev, ring);
			prev = ring;
		}
	}
	for (size_t i = 0; i < n; ++i) {
		const int ring = ringAt(pts[i], T[i], U[i], PickF(r, i, 0.05f), sAt[i], total > 0.0f ? sAt[i] / total : 0.0f, 0.0f);
		if (prev >= 0) stitch(prev, ring);
		prev = ring;
	}
	if (capEnd) {
		const float rad = PickF(r, n - 1, 0.05f);
		for (int j = 1; j <= capRings; ++j) {
			const float phi = (kFxPi * 0.5f) * static_cast<float>(j) / static_cast<float>(capRings);
			const Vec3 c = pts[n - 1] + T[n - 1] * (rad * std::sin(phi));
			const int ring = ringAt(c, T[n - 1], U[n - 1], MaxF(rad * std::cos(phi), rad * 0.02f),
			                        total + rad * std::sin(phi), 1.0f, std::sin(phi));
			stitch(prev, ring);
			prev = ring;
		}
	}
}

void AppendPathStrip(MeshData& m, const std::vector<Vec3>& pts, const std::vector<float>& widths, const StripParams& sp) {
	const int n = static_cast<int>(pts.size());
	if (n < 2) return;
	const int cross = MaxI(sp.cross, 3);
	const int frontCap = MaxI(sp.frontCap, 1);
	const int tailCap = 1;
	const int rings = n + frontCap + tailCap;
	std::vector<Vec3> rc(static_cast<size_t>(rings)), rside(static_cast<size_t>(rings)), rup(static_cast<size_t>(rings));
	std::vector<float> rhw(static_cast<size_t>(rings)), rh(static_cast<size_t>(rings)), rs(static_cast<size_t>(rings));
	std::vector<float> arc(static_cast<size_t>(n), 0.0f);
	float total = 0.0f;
	for (int i = 1; i < n; ++i) {
		total += pts[static_cast<size_t>(i)].distance_to(pts[static_cast<size_t>(i - 1)]);
		arc[static_cast<size_t>(i)] = total;
	}
	const Vec3 up(0.0f, 1.0f, 0.0f);
	Vec3 tailDir(0.0f, 0.0f, 1.0f), frontDir(0.0f, 0.0f, 1.0f);
	for (int i = 0; i < n; ++i) {
		const Vec3 a = pts[static_cast<size_t>(MaxI(i - 1, 0))];
		const Vec3 b = pts[static_cast<size_t>(MinI(i + 1, n - 1))];
		Vec3 t = Flat(b - a);
		if (t.length_squared() < 1e-8f) t = Flat(pts[static_cast<size_t>(n - 1)] - pts[0]);
		t = Norm(t, Vec3(0.0f, 0.0f, 1.0f));
		const float k = arc[static_cast<size_t>(i)] / MaxF(total, 0.001f);
		float hw = MaxF(PickF(widths, static_cast<size_t>(i), 1.0f), 0.05f) * 0.5f;
		float h = Lerp(sp.heightTail, sp.heightFront, Smooth(0.0f, 1.0f, k));
		h *= 1.0f + sp.frontBulge * Smooth(0.55f, 1.0f, k);
		hw *= 1.0f + sp.widthBulge * Smooth(0.7f, 1.0f, k);
		const size_t r = static_cast<size_t>(i + tailCap);
		rc[r] = pts[static_cast<size_t>(i)];
		// side = up x t points to the right of travel in a right-handed Y-up frame when t = +Z: (0,1,0)x(0,0,1) = +X
		const Vec3 side = Norm(up.cross(t), Vec3(1.0f, 0.0f, 0.0f));
		Vec3 f3 = b - a;
		f3 = f3.length_squared() > 1e-10f ? Norm(f3) : t;
		Vec3 upn = Norm(f3.cross(side), up);   // slope aware: lava drapes over ledges
		if (upn.y < 0.05f) upn = Norm(upn + up * 0.3f);
		rside[r] = side;
		rup[r] = upn;
		rhw[r] = hw;
		rh[r] = h;
		rs[r] = arc[static_cast<size_t>(i)];
		if (i == 0) tailDir = t;
		if (i == n - 1) frontDir = t;
	}
	// tail cap: narrow, low ring behind the first point
	rc[0] = pts[0] - tailDir * (rhw[1] * 0.5f);
	rside[0] = rside[1];
	rup[0] = rup[1];
	rhw[0] = rhw[1] * 0.25f;
	rh[0] = rh[1] * 0.2f;
	rs[0] = -rhw[1] * 0.5f;
	// front cap: quarter ellipse rounding the leading edge
	const size_t last = static_cast<size_t>(n - 1 + tailCap);
	const float capLen = rhw[last] * 0.6f;
	for (int j = 1; j <= frontCap; ++j) {
		const float phi = static_cast<float>(j) / static_cast<float>(frontCap) * kFxPi * 0.5f;
		const size_t r = last + static_cast<size_t>(j);
		rc[r] = pts[static_cast<size_t>(n - 1)] + frontDir * (capLen * std::sin(phi));
		rside[r] = rside[last];
		rup[r] = rup[last];
		rhw[r] = MaxF(rhw[last] * std::cos(phi), 0.002f);
		rh[r] = rh[last] * std::pow(MaxF(std::cos(phi), 0.0f), 0.65f);
		rs[r] = total + capLen * std::sin(phi);
	}
	const float frontS = total + capLen;
	const int base = m.NumVerts();
	for (int r = 0; r < rings; ++r) {
		const size_t ru = static_cast<size_t>(r);
		for (int k = 0; k < cross; ++k) {
			const float th = -kFxPi * 0.5f + kFxPi * static_cast<float>(k) / static_cast<float>(cross - 1);
			const float u = std::sin(th), v = std::cos(th);
			const Vec3 p = rc[ru] + rside[ru] * (rhw[ru] * u) + rup[ru] * (rh[ru] * v);
			m.Add(p, rup[ru], Vec2(rs[ru], rhw[ru] * u), Color(), Vec2(Sat(rs[ru] / MaxF(frontS, 1e-3f)), u),
			      Vec2(frontS - rs[ru], v), Vec3(1.0f, 0.0f, 0.0f));
		}
	}
	for (int r = 0; r + 1 < rings; ++r) {
		for (int k = 0; k + 1 < cross; ++k) {
			const int a = base + r * cross + k;
			const int b = a + cross;
			// k runs from the left bank (u = -1, side * -hw) to the right bank; r runs tail -> front.
			// cross((a+1) - a, b - a) ~ side x fwd = (up x t) x t = -up ... so wind a, b, b+1 / a, b+1, a+1:
			// cross(b - a, (b+1) - a) ~ fwd x side = t x (up x t) = up (outward on the top).
			m.Tri(a, b, b + 1);
			m.Tri(a, b + 1, a + 1);
		}
	}
	// smooth normals over this strip only, then tangents along the flow
	std::vector<Vec3> acc(static_cast<size_t>(rings * cross), Vec3());
	const size_t i0 = m.idx.size() - static_cast<size_t>((rings - 1) * (cross - 1) * 6);
	for (size_t i = i0; i + 2 < m.idx.size(); i += 3) {
		const size_t a = static_cast<size_t>(m.idx[i]), b = static_cast<size_t>(m.idx[i + 1]), c = static_cast<size_t>(m.idx[i + 2]);
		const Vec3 fn = (m.pos[b] - m.pos[a]).cross(m.pos[c] - m.pos[a]);
		acc[a - static_cast<size_t>(base)] += fn;
		acc[b - static_cast<size_t>(base)] += fn;
		acc[c - static_cast<size_t>(base)] += fn;
	}
	for (int r = 0; r < rings; ++r) {
		const int ra = MaxI(r - 1, 0), rb = MinI(r + 1, rings - 1);
		for (int k = 0; k < cross; ++k) {
			const size_t vi = static_cast<size_t>(base + r * cross + k);
			const Vec3 nn = Norm(acc[vi - static_cast<size_t>(base)], up);
			m.nrm[vi] = nn;
			Vec3 tv = m.pos[static_cast<size_t>(base + rb * cross + k)] - m.pos[static_cast<size_t>(base + ra * cross + k)];
			tv = tv - nn * tv.dot(nn);
			m.tan[vi] = Norm(tv, Norm(rside[static_cast<size_t>(r)].cross(nn)));
		}
	}
}

void AppendSphere(MeshData& m, const Vec3& c, const Vec3& radii, int rings, int segs, bool hemisphere, const Color& col) {
	rings = MaxI(rings, 2);
	segs = MaxI(segs, 3);
	const int base = m.NumVerts();
	for (int i = 0; i <= rings; ++i) {
		const float v = static_cast<float>(i) / static_cast<float>(rings);
		const float th = hemisphere ? (kFxPi * 0.5f) * (1.0f - v) : kFxPi * (1.0f - v);   // polar angle from +Y
		const float sy = std::cos(th), sr = std::sin(th);
		for (int k = 0; k <= segs; ++k) {
			const float u = static_cast<float>(k) / static_cast<float>(segs);
			const float ph = u * kFxTau;
			const Vec3 d(sr * std::cos(ph), sy, -sr * std::sin(ph));
			const Vec3 p = c + Vec3(d.x * radii.x, d.y * radii.y, d.z * radii.z);
			const Vec3 n = Norm(Vec3(d.x / MaxF(radii.x, 1e-6f), d.y / MaxF(radii.y, 1e-6f), d.z / MaxF(radii.z, 1e-6f)), d);
			const Vec3 t = Norm(Vec3(-std::sin(ph), 0.0f, -std::cos(ph)));
			m.Add(p, n, Vec2(u, v), col, Vec2(), Vec2(), t);
		}
	}
	for (int i = 0; i < rings; ++i)
		for (int k = 0; k < segs; ++k) {
			const int a = base + i * (segs + 1) + k;   // lower ring
			const int b = a + segs + 1;                // upper ring
			// ph increases counter-clockwise seen from +Y (x = cos, z = -sin): (a+1 - a) x (b - a) ~ tangent x up
			// -> outward. Skip degenerate pole triangles.
			if (!(i == 0 && !hemisphere)) m.Tri(a, a + 1, b + 1);
			if (i + 1 < rings) m.Tri(a, b + 1, b);
		}
}

void AppendLathe(MeshData& m, const Vec3& base, const Vec3& axis, float height, const std::vector<float>& rad, int segs,
                 float twist, const Color& col, bool inward) {
	const int n = static_cast<int>(rad.size());
	if (n < 2 || segs < 3) return;
	const Vec3 ax = Norm(axis);
	const Vec3 u = Perp(ax);
	const Vec3 v = ax.cross(u);
	const int b0 = m.NumVerts();
	for (int i = 0; i < n; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(n - 1);
		const float r = rad[static_cast<size_t>(i)];
		// profile slope for normals
		const float rp = rad[static_cast<size_t>(MinI(i + 1, n - 1))] - rad[static_cast<size_t>(MaxI(i - 1, 0))];
		const float dt = (static_cast<float>(MinI(i + 1, n - 1) - MaxI(i - 1, 0)) / static_cast<float>(n - 1)) * height;
		for (int k = 0; k <= segs; ++k) {
			const float a = kFxTau * static_cast<float>(k) / static_cast<float>(segs) + twist * t;
			const Vec3 d = u * std::cos(a) + v * std::sin(a);
			Vec3 nn = Norm(d * dt - ax * rp, d);
			if (inward) nn = nn * -1.0f;
			const Vec3 tg = Norm(ax.cross(d));
			m.Add(base + ax * (t * height) + d * r, nn, Vec2(static_cast<float>(k) / static_cast<float>(segs), t), col,
			      Vec2(t, 0.0f), Vec2(), tg);
		}
	}
	for (int i = 0; i + 1 < n; ++i)
		for (int k = 0; k < segs; ++k) {
			const int a = b0 + i * (segs + 1) + k;
			const int b = a + segs + 1;
			// angle increases counter-clockwise around ax (d = u cos + v sin, v = ax x u):
			// (a+1 - a) x (b - a) ~ (ax x d) x ax = d (outward)
			if (inward) m.Quad(a, b, b + 1, a + 1);
			else m.Quad(a, a + 1, b + 1, b);
		}
}

void AppendRing(MeshData& m, const Vec3& c, const Vec3& normal, float r0, float r1, int segs, const Color& col,
                float arcStart, float arcLen) {
	segs = MaxI(segs, 3);
	const Vec3 nn = Norm(normal);
	const Vec3 u = Perp(nn);
	const Vec3 v = nn.cross(u);
	const int b0 = m.NumVerts();
	for (int k = 0; k <= segs; ++k) {
		const float f = static_cast<float>(k) / static_cast<float>(segs);
		const float a = arcStart + arcLen * f;
		const Vec3 d = u * std::cos(a) + v * std::sin(a);
		const Vec3 tg = nn.cross(d);
		m.Add(c + d * r0, nn, Vec2(f, 0.0f), col, Vec2(), Vec2(), tg);
		m.Add(c + d * r1, nn, Vec2(f, 1.0f), col, Vec2(), Vec2(), tg);
	}
	for (int k = 0; k < segs; ++k) {
		const int a = b0 + k * 2;
		// angle ccw around nn: (outer - inner) x (next inner - inner) ~ d x (nn x d) = nn
		m.Quad(a, a + 1, a + 3, a + 2);
	}
}

void AppendChamferBox(MeshData& m, const Vec3& c, const Vec3& h, float r, const Basis& b, float rnd, const Color& col) {
	const float rr = MinF(r, MinF(h.x, MinF(h.y, h.z)) * 0.45f);
	const Vec3 inner(h.x - rr, h.y - rr, h.z - rr);
	auto coord = [](float hh, float ch, int i) {
		switch (i) {
			case 0: return -hh;
			case 1: return -hh + ch;
			case 2: return hh - ch;
			default: return hh;
		}
	};
	// six faces: normal axis, sign; (u, v) axes chosen so u x v = outward normal
	struct Face { int ax, au, av; float sgn; };
	static const Face kFaces[6] = {{0, 1, 2, 1.0f}, {0, 2, 1, -1.0f}, {1, 2, 0, 1.0f}, {1, 0, 2, -1.0f}, {2, 0, 1, 1.0f}, {2, 1, 0, -1.0f}};
	for (const Face& f : kFaces) {
		const int first = m.NumVerts();
		for (int j = 0; j < 4; ++j)
			for (int i = 0; i < 4; ++i) {
				Vec3 p;
				p[f.ax] = f.sgn * h[f.ax];
				p[f.au] = coord(h[f.au], rr, i);
				p[f.av] = coord(h[f.av], rr, j);
				const Vec3 cl(Clamp(p.x, -inner.x, inner.x), Clamp(p.y, -inner.y, inner.y), Clamp(p.z, -inner.z, inner.z));
				Vec3 d = p - cl;
				const Vec3 nl = Norm(d);
				// chamfer: pull the edge vertices onto the rounded edge (45 degree bevel for one ring)
				const Vec3 q = cl + nl * rr;
				Vec3 uvp(q[f.au], q[f.av], 0.0f);
				m.Add(c + b.Apply(q), Norm(b.Apply(nl)), Vec2(uvp.x, uvp.y), col, Vec2(), Vec2(0.0f, rnd),
				      Norm(b.Apply(Vec3(f.au == 0 ? 1.0f : 0.0f, f.au == 1 ? 1.0f : 0.0f, f.au == 2 ? 1.0f : 0.0f))));
			}
		for (int j = 0; j < 3; ++j)
			for (int i = 0; i < 3; ++i) {
				const int a = first + j * 4 + i;
				// u increases with i, v with j; with u x v = outward: (a+1 - a) x (a+4 - a) = outward
				m.Quad(a, a + 1, a + 5, a + 4);
			}
	}
}

// ---------------------------------------------------------------- procedural rock (fallback when no Blender rock)
static void Icosphere(std::vector<Vec3>& v, std::vector<int32_t>& f, int subdiv) {
	const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
	v = {Norm(Vec3(-1, t, 0)), Norm(Vec3(1, t, 0)), Norm(Vec3(-1, -t, 0)), Norm(Vec3(1, -t, 0)),
	     Norm(Vec3(0, -1, t)), Norm(Vec3(0, 1, t)), Norm(Vec3(0, -1, -t)), Norm(Vec3(0, 1, -t)),
	     Norm(Vec3(t, 0, -1)), Norm(Vec3(t, 0, 1)), Norm(Vec3(-t, 0, -1)), Norm(Vec3(-t, 0, 1))};
	f = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
	     3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
	for (int s = 0; s < subdiv; ++s) {
		std::vector<int32_t> nf;
		std::vector<std::pair<uint64_t, int32_t>> cache;
		auto mid = [&](int32_t a, int32_t b) {
			const uint64_t key = a < b ? (static_cast<uint64_t>(a) << 32) | static_cast<uint32_t>(b)
			                           : (static_cast<uint64_t>(b) << 32) | static_cast<uint32_t>(a);
			for (const auto& kv : cache)
				if (kv.first == key) return kv.second;
			v.push_back(Norm((v[static_cast<size_t>(a)] + v[static_cast<size_t>(b)]) * 0.5f));
			const int32_t id = static_cast<int32_t>(v.size()) - 1;
			cache.emplace_back(key, id);
			return id;
		};
		for (size_t i = 0; i + 2 < f.size(); i += 3) {
			const int32_t a = f[i], b = f[i + 1], c = f[i + 2];
			const int32_t ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
			nf.insert(nf.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
		}
		f.swap(nf);
	}
}

static float RockNoise(const Vec3& p, uint32_t seed) {
	// sum of a few random planar cuts + smooth bumps: chunky, cube-cut silhouettes
	float n = 0.0f;
	Rng r(seed);
	for (int i = 0; i < 7; ++i) {
		const Vec3 d = r.OnSphere();
		const float off = r.Range(0.55f, 0.9f);
		const float k = p.dot(d) - off;
		if (k > 0.0f) n -= k * 0.85f;   // cut
	}
	for (int i = 0; i < 4; ++i) {
		const Vec3 d = r.OnSphere();
		n += 0.05f * std::sin(p.dot(d) * r.Range(3.0f, 7.0f) + r.Range(0.0f, 6.0f));
	}
	return n;
}

void AppendRock(MeshData& m, uint32_t seed, float radius, const Vec3& c) {
	std::vector<Vec3> v;
	std::vector<int32_t> f;
	Icosphere(v, f, 2);
	Rng r(seed * 2654435761u + 17u);
	const Vec3 sc(r.Range(0.85f, 1.15f), r.Range(0.7f, 0.95f), r.Range(0.85f, 1.15f));
	std::vector<Vec3> p(v.size());
	std::vector<float> rnd(v.size());
	for (size_t i = 0; i < v.size(); ++i) {
		const Vec3 d = v[i];
		const float k = 1.0f + RockNoise(d, seed);
		p[i] = Vec3(d.x * sc.x, d.y * sc.y, d.z * sc.z) * MaxF(k, 0.45f);
		rnd[i] = Hash01(seed * 977u + static_cast<uint32_t>(i));
	}
	// faceted: unshared vertices per triangle (flat normals), blob = ellipsoid with the rock's proportions
	for (size_t i = 0; i + 2 < f.size(); i += 3) {
		const size_t a = static_cast<size_t>(f[i]), b = static_cast<size_t>(f[i + 1]), cc = static_cast<size_t>(f[i + 2]);
		Vec3 fn = (p[b] - p[a]).cross(p[cc] - p[a]);
		// icosphere faces as listed are wound outward (cross points away from the centre); keep it that way
		if (fn.dot(p[a] + p[b] + p[cc]) < 0.0f) fn = fn * -1.0f;
		fn = Norm(fn);
		int ids[3];
		const size_t tri[3] = {a, b, cc};
		for (int j = 0; j < 3; ++j) {
			const size_t s = tri[j];
			const Vec3 blob = Vec3(v[s].x * sc.x, v[s].y * sc.y * 0.92f, v[s].z * sc.z) * 0.9f;
			const Vec3 off = (blob - p[s]) * radius;   // blob offset in mesh units
			ids[j] = m.Add(c + p[s] * radius, fn, Vec2(0.5f + 0.5f * v[s].x, 0.5f - 0.5f * v[s].y), Color(),
			               Vec2(off.x, off.y), Vec2(off.z, rnd[s]), Perp(fn));
		}
		const Vec3 wn = (m.pos[static_cast<size_t>(ids[1])] - m.pos[static_cast<size_t>(ids[0])])
		                    .cross(m.pos[static_cast<size_t>(ids[2])] - m.pos[static_cast<size_t>(ids[0])]);
		if (wn.dot(fn) >= 0.0f) m.Tri(ids[0], ids[1], ids[2]);
		else m.Tri(ids[0], ids[2], ids[1]);
	}
}

void AppendSpike(MeshData& m, const Vec3& c, const Vec3& up, float r, float h, int facets, float twist, const Color& col) {
	facets = MaxI(facets, 3);
	const Vec3 ax = Norm(up);
	const Vec3 u = Perp(ax);
	const Vec3 v = ax.cross(u);
	const Vec3 tip = c + ax * h + (u * std::cos(twist) + v * std::sin(twist)) * (r * 0.15f);
	for (int k = 0; k < facets; ++k) {
		const float a0 = kFxTau * static_cast<float>(k) / static_cast<float>(facets) + twist;
		const float a1 = kFxTau * static_cast<float>(k + 1) / static_cast<float>(facets) + twist;
		const Vec3 p0 = c + (u * std::cos(a0) + v * std::sin(a0)) * r;
		const Vec3 p1 = c + (u * std::cos(a1) + v * std::sin(a1)) * r;
		Vec3 fn = (p1 - p0).cross(tip - p0);
		fn = Norm(fn);
		const float f0 = static_cast<float>(k) / static_cast<float>(facets);
		const float f1 = static_cast<float>(k + 1) / static_cast<float>(facets);
		const int a = m.Add(p0, fn, Vec2(f0, 0.0f), col, Vec2(0.0f, 0.0f), Vec2(), Norm(p1 - p0));
		const int b = m.Add(p1, fn, Vec2(f1, 0.0f), col, Vec2(0.0f, 0.0f), Vec2(), Norm(p1 - p0));
		const int t = m.Add(tip, fn, Vec2((f0 + f1) * 0.5f, 1.0f), col, Vec2(1.0f, 0.0f), Vec2(), Norm(p1 - p0));
		m.Tri(a, b, t);
	}
}

void AppendCrescent(MeshData& m, const Vec3& c, const Vec3& fwd, const Vec3& normal, float R, float w, float arcLen,
                    int segs, const Color& col) {
	segs = MaxI(segs, 4);
	const Vec3 nn = Norm(normal);
	const Vec3 f = Norm(fwd - nn * fwd.dot(nn), Perp(nn));
	const Vec3 s = nn.cross(f);
	const int b0 = m.NumVerts();
	for (int k = 0; k <= segs; ++k) {
		const float t = static_cast<float>(k) / static_cast<float>(segs);
		const float a = (t - 0.5f) * arcLen;
		const Vec3 d = f * std::cos(a) + s * std::sin(a);
		const float taper = std::sin(t * kFxPi);
		const float hw = 0.5f * w * MaxF(taper, 0.02f);
		// inner edge trails behind the outer (a crescent bulging forward)
		const Vec3 outer = c + d * (R + hw * 0.6f);
		const Vec3 inner = c + d * (R - hw * 1.4f) - f * (hw * 0.8f * taper);
		const Vec3 tg = nn.cross(d);
		m.Add(inner, nn, Vec2(t, 0.0f), col, Vec2(t, 0.0f), Vec2(), tg);
		m.Add(outer, nn, Vec2(t, 1.0f), col, Vec2(t, 1.0f), Vec2(), tg);
	}
	for (int k = 0; k < segs; ++k) {
		const int a = b0 + k * 2;
		// angle increases from -arc/2 toward +s (ccw around nn): (outer - inner) x (next - inner) ~ d x (nn x d) = nn
		m.Quad(a, a + 1, a + 3, a + 2);
	}
}

}  // namespace ffx
