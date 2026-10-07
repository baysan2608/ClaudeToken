// FourfoldFX logic island - Voronoi fracture pieces (see FxFracture.h). Owner: stream `fx`.
#include "FxFracture.h"

#include "FxMeshLib.h"

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

namespace ffx {

namespace {

struct CVert {
	Vec3 p, n, t;
	Vec2 uv0, uv1, uv2;
	Color col;
};

CVert GetV(const MeshData& m, int32_t i) {
	const size_t s = static_cast<size_t>(i);
	return {m.pos[s], m.nrm[s], m.tan[s], m.uv0[s], m.uv1[s], m.uv2[s], m.col[s]};
}

int PutV(MeshData& m, const CVert& v) { return m.Add(v.p, v.n, v.uv0, v.col, v.uv1, v.uv2, v.t); }

Vec2 LerpV2(const Vec2& a, const Vec2& b, float t) { return Vec2(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)); }

CVert LerpCV(const CVert& a, const CVert& b, float t) {
	CVert v;
	v.p = LerpV(a.p, b.p, t);
	v.n = Norm(LerpV(a.n, b.n, t), a.n);
	v.t = Norm(LerpV(a.t, b.t, t), a.t);
	v.uv0 = LerpV2(a.uv0, b.uv0, t);
	v.uv1 = LerpV2(a.uv1, b.uv1, t);
	v.uv2 = LerpV2(a.uv2, b.uv2, t);
	v.col = LerpC(a.col, b.col, t);
	return v;
}

// Quantised positions: the cut point of one geometric edge must be identical in every face sharing it (faceted meshes
// duplicate their vertices per face).
using QKey = std::array<int64_t, 3>;
using EdgeKey = std::array<int64_t, 6>;

QKey Quant(const Vec3& p) {
	constexpr double kQ = 1e5;   // 10 micrometres
	return {std::llround(static_cast<double>(p.x) * kQ), std::llround(static_cast<double>(p.y) * kQ),
	        std::llround(static_cast<double>(p.z) * kQ)};
}

EdgeKey EdgeOf(const QKey& a, const QKey& b) {
	const QKey& lo = a < b ? a : b;
	const QKey& hi = a < b ? b : a;
	return {lo[0], lo[1], lo[2], hi[0], hi[1], hi[2]};
}

// Cap segment: from the entry to the exit cut point of one clipped face (the reverse of the kept face's cut edge, so
// chained segments wind the cap the same way as the rest of the surface).
struct Seg {
	EdgeKey from{}, to{};
	Vec3 p;      // position of `from`
	Color col;
};

// 42 directions (icosphere level 1) for the hull support points.
const std::vector<Vec3>& HullDirs() {
	static const std::vector<Vec3> dirs = [] {
		const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
		std::vector<Vec3> v = {Norm(Vec3(-1, t, 0)), Norm(Vec3(1, t, 0)),  Norm(Vec3(-1, -t, 0)), Norm(Vec3(1, -t, 0)),
		                       Norm(Vec3(0, -1, t)), Norm(Vec3(0, 1, t)),  Norm(Vec3(0, -1, -t)), Norm(Vec3(0, 1, -t)),
		                       Norm(Vec3(t, 0, -1)), Norm(Vec3(t, 0, 1)),  Norm(Vec3(-t, 0, -1)), Norm(Vec3(-t, 0, 1))};
		// edge midpoints of the icosahedron: every pair at the edge length
		const size_t n = v.size();
		const float edge = (v[0] - v[1]).length();
		for (size_t i = 0; i < n; ++i)
			for (size_t j = i + 1; j < n; ++j)
				if (std::fabs((v[i] - v[j]).length() - edge) < 1e-3f) v.push_back(Norm(v[i] + v[j]));
		return v;
	}();
	return dirs;
}

std::vector<Vec3> SupportPoints(const MeshData& m) {
	std::vector<Vec3> out;
	if (m.pos.empty()) return out;
	for (const Vec3& d : HullDirs()) {
		size_t best = 0;
		float bestDot = -1e30f;
		for (size_t i = 0; i < m.pos.size(); ++i) {
			const float k = m.pos[i].dot(d);
			if (k > bestDot) {
				bestDot = k;
				best = i;
			}
		}
		const Vec3& p = m.pos[best];
		bool dup = false;
		for (const Vec3& q : out) dup = dup || (q - p).length_squared() < 1e-10f;
		if (!dup) out.push_back(p);
	}
	return out;
}

}  // namespace

float MeshVolume(const MeshData& m, Vec3* centroid) {
	double vol = 0.0, cx = 0.0, cy = 0.0, cz = 0.0;
	for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
		const Vec3& a = m.pos[static_cast<size_t>(m.idx[i])];
		const Vec3& b = m.pos[static_cast<size_t>(m.idx[i + 1])];
		const Vec3& c = m.pos[static_cast<size_t>(m.idx[i + 2])];
		const double v = static_cast<double>(a.dot(b.cross(c))) / 6.0;   // tetrahedron (origin, a, b, c)
		vol += v;
		cx += v * static_cast<double>(a.x + b.x + c.x) / 4.0;
		cy += v * static_cast<double>(a.y + b.y + c.y) / 4.0;
		cz += v * static_cast<double>(a.z + b.z + c.z) / 4.0;
	}
	if (centroid) {
		*centroid = std::fabs(vol) > 1e-12 ? Vec3(static_cast<float>(cx / vol), static_cast<float>(cy / vol), static_cast<float>(cz / vol))
		                                   : Vec3();
	}
	return static_cast<float>(vol);
}

bool ClipClosedMesh(const MeshData& in, const Vec3& n, float d, MeshData& out) {
	out.Clear();
	std::vector<Seg> segs;
	for (size_t i = 0; i + 2 < in.idx.size(); i += 3) {
		CVert v[3];
		float s[3];
		bool inside[3];
		int nIn = 0;
		for (int k = 0; k < 3; ++k) {
			v[k] = GetV(in, in.idx[i + static_cast<size_t>(k)]);
			s[k] = n.dot(v[k].p) - d;
			if (std::fabs(s[k]) < 1e-6f) s[k] = 0.0f;   // on the plane counts as kept: no slivers
			inside[k] = s[k] <= 0.0f;
			nIn += inside[k] ? 1 : 0;
		}
		if (nIn == 0) continue;
		if (nIn == 3) {
			const int a = PutV(out, v[0]), b = PutV(out, v[1]), c = PutV(out, v[2]);
			out.Tri(a, b, c);
			continue;
		}
		CVert poly[4];
		int np = 0;
		Seg seg;
		for (int e = 0; e < 3; ++e) {
			const int a = e, b = (e + 1) % 3;
			if (inside[a]) poly[np++] = v[a];
			if (inside[a] == inside[b]) continue;
			// cut point from the edge's canonical direction (identical in both faces sharing the edge)
			const QKey ka = Quant(v[a].p), kb = Quant(v[b].p);
			const bool aLo = ka < kb;
			const float sLo = aLo ? s[a] : s[b], sHi = aLo ? s[b] : s[a];
			const float tLo = Sat(sLo / (sLo - sHi));
			const Vec3 p = LerpV(aLo ? v[a].p : v[b].p, aLo ? v[b].p : v[a].p, tLo);
			CVert cv = LerpCV(v[a], v[b], aLo ? tLo : 1.0f - tLo);
			cv.p = p;
			poly[np++] = cv;
			const EdgeKey ek = EdgeOf(ka, kb);
			if (inside[a]) {
				seg.to = ek;   // exit
			} else {
				seg.from = ek;   // entry
				seg.p = p;
				seg.col = cv.col;
			}
		}
		int ids[4] = {0, 0, 0, 0};
		for (int k = 0; k < np; ++k) ids[k] = PutV(out, poly[k]);
		out.Tri(ids[0], ids[1], ids[2]);
		if (np == 4) out.Tri(ids[0], ids[2], ids[3]);
		segs.push_back(seg);
	}
	// caps: chain the segments into loops and fan each loop from its centroid (Voronoi cuts of a near-convex solid
	// give convex sections)
	std::map<EdgeKey, size_t> byFrom;
	for (size_t i = 0; i < segs.size(); ++i) byFrom[segs[i].from] = i;
	std::vector<char> used(segs.size(), 0);
	const Vec3 ta = Perp(n), tb = n.cross(ta);
	for (size_t i0 = 0; i0 < segs.size(); ++i0) {
		if (used[i0]) continue;
		std::vector<size_t> loop;
		bool closed = false;
		for (size_t i = i0; !used[i];) {
			used[i] = 1;
			loop.push_back(i);
			const auto it = byFrom.find(segs[i].to);
			if (it == byFrom.end()) break;
			i = it->second;
			if (i == i0) {
				closed = true;
				break;
			}
		}
		if (!closed || loop.size() < 3) continue;
		Vec3 c, newell;
		Color col(0.0f, 0.0f, 0.0f, 0.0f);
		for (size_t k = 0; k < loop.size(); ++k) {
			const Vec3& p = segs[loop[k]].p;
			const Vec3& q = segs[loop[(k + 1) % loop.size()]].p;
			c += p;
			newell += p.cross(q);
			col = col + segs[loop[k]].col;
		}
		const float inv = 1.0f / static_cast<float>(loop.size());
		c = c * inv;
		col = col * inv;
		const bool flip = newell.dot(n) < 0.0f;
		// uv2.y = -1 marks a fresh break for the rock material (its rocks keep uv2.y = random 0..1); no blob offset
		auto capV = [&](const Vec3& p) {
			return out.Add(p, n, Vec2(p.dot(ta), p.dot(tb)), col, Vec2(0.0f, 0.0f), Vec2(0.0f, -1.0f), ta);
		};
		const int ic = capV(c);
		std::vector<int> ring(loop.size());
		for (size_t k = 0; k < loop.size(); ++k) ring[k] = capV(segs[loop[k]].p);
		for (size_t k = 0; k < ring.size(); ++k) {
			const int a = ring[k], b = ring[(k + 1) % ring.size()];
			if (flip) out.Tri(ic, b, a);
			else out.Tri(ic, a, b);
		}
	}
	return !out.Empty();
}

std::vector<FracturePiece> VoronoiPieces(const MeshData& src, const std::vector<Vec3>& sites) {
	std::vector<FracturePiece> pieces;
	const float total = MeshVolume(src);
	MeshData a, b;
	for (size_t i = 0; i < sites.size(); ++i) {
		a.Clear();
		a.Append(src);
		bool alive = true;
		for (size_t j = 0; j < sites.size() && alive; ++j) {
			if (j == i) continue;
			const Vec3 dir = sites[j] - sites[i];
			if (dir.length_squared() < 1e-12f) continue;
			const Vec3 nrm = Norm(dir);
			alive = ClipClosedMesh(a, nrm, nrm.dot((sites[i] + sites[j]) * 0.5f), b);
			std::swap(a, b);
		}
		if (!alive) continue;
		FracturePiece p;
		p.volume = MeshVolume(a, &p.centre);
		if (p.volume < total * 0.004f) continue;
		p.mesh.Append(a);   // fresh MeshData: its own uid on Commit
		p.mesh.Commit();
		p.hull = SupportPoints(p.mesh);
		pieces.push_back(std::move(p));
	}
	return pieces;
}

std::vector<Vec3> FractureSites(uint32_t seed, int n, const Vec3& centre, const Vec3& halfExtents) {
	std::vector<Vec3> sites;
	Rng r(HashCombine(seed, 0x51ed27u));
	for (int k = 0; k < n; ++k) {
		Vec3 best;
		float bestD = -1.0f;
		for (int tries = 0; tries < 12; ++tries) {
			const Vec3 u = r.InSphere();
			const Vec3 p = centre + Vec3(u.x * halfExtents.x, u.y * halfExtents.y, u.z * halfExtents.z);
			float dmin = 1e30f;
			for (const Vec3& q : sites) dmin = MinF(dmin, (q - p).length_squared());
			if (dmin > bestD) {
				bestD = dmin;
				best = p;
			}
		}
		sites.push_back(best);
	}
	return sites;
}

namespace meshlib {

namespace {
using PieceCache = std::map<uint32_t, std::unique_ptr<std::vector<FracturePiece>>>;

Vec3 BoundsOf(const MeshData& m, Vec3& centre) {
	Vec3 lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
	for (const Vec3& p : m.pos) {
		lo = Vec3(MinF(lo.x, p.x), MinF(lo.y, p.y), MinF(lo.z, p.z));
		hi = Vec3(MaxF(hi.x, p.x), MaxF(hi.y, p.y), MaxF(hi.z, p.z));
	}
	centre = (lo + hi) * 0.5f;
	return (hi - lo) * 0.5f;
}
}  // namespace

const std::vector<FracturePiece>& RockPieces(uint32_t seed, int n) {
	static PieceCache cache;
	n = ClampI(n, 2, 8);
	const uint32_t key = (seed % 64u) * 16u + static_cast<uint32_t>(n);
	auto& slot = cache[key];
	if (!slot) {
		const MeshData& rock = Rock(seed);
		Vec3 c;
		const Vec3 h = BoundsOf(rock, c);
		slot = std::make_unique<std::vector<FracturePiece>>(VoronoiPieces(rock, FractureSites(seed, n, c, h * 0.55f)));
	}
	return *slot;
}

const std::vector<FracturePiece>& WallPieces(uint32_t seed, int perBlock) {
	static PieceCache cache;
	perBlock = ClampI(perBlock, 1, 4);
	const uint32_t key = (seed % 16u) * 8u + static_cast<uint32_t>(perBlock);
	auto& slot = cache[key];
	if (!slot) {
		slot = std::make_unique<std::vector<FracturePiece>>();
		std::vector<MeshData> blocks;
		BuildWallBlocks(blocks, seed % 16u, true);
		for (size_t b = 0; b < blocks.size(); ++b) {
			if (perBlock == 1) {
				FracturePiece p;
				p.volume = MeshVolume(blocks[b], &p.centre);
				p.mesh.Append(blocks[b]);
				p.mesh.Commit();
				p.hull = SupportPoints(p.mesh);
				slot->push_back(std::move(p));
				continue;
			}
			Vec3 c;
			const Vec3 h = BoundsOf(blocks[b], c);
			std::vector<FracturePiece> ps = VoronoiPieces(
				blocks[b], FractureSites(HashCombine(seed, static_cast<uint32_t>(b)), perBlock, c, h * 0.6f));
			for (FracturePiece& p : ps) slot->push_back(std::move(p));
		}
	}
	return *slot;
}

}  // namespace meshlib

}  // namespace ffx
