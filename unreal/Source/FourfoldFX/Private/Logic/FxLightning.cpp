// FourfoldFX logic island - branching lightning generator. Owner: stream `fx`.
#include "FxLightning.h"

#include <cmath>

namespace ffx {

namespace {

// Recursive midpoint displacement of the span a -> b (exclusive of a, inclusive of b) appended to `pts`.
void Subdivide(std::vector<Vec3>& pts, const Vec3& a, const Vec3& b, float disp, int level, const BoltParams& p, Rng& rng) {
	const Vec3 d = b - a;
	const float len = d.length();
	if (level >= p.maxLevels || len <= p.maxSegment) {
		pts.push_back(b);
		return;
	}
	const Vec3 dir = d / MaxF(len, 1e-6f);
	const Vec3 u = Perp(dir);
	const Vec3 v = dir.cross(u);
	const float ang = rng.F01() * kFxTau;
	const float mag = disp * len * (0.35f + 0.65f * rng.F01());
	// slight along-span shift keeps the kinks irregular
	const Vec3 mid = a + d * (0.5f + 0.12f * rng.Signed()) + (u * std::cos(ang) + v * std::sin(ang)) * mag;
	Subdivide(pts, a, mid, disp * p.roughness, level + 1, p, rng);
	Subdivide(pts, mid, b, disp * p.roughness, level + 1, p, rng);
}

float PathLength(const std::vector<Vec3>& pts) {
	float t = 0.0f;
	for (size_t i = 1; i < pts.size(); ++i) t += pts[i].distance_to(pts[i - 1]);
	return t;
}

}  // namespace

int GenerateBolt(const std::vector<Vec3>& nodes, uint32_t seed, const BoltParams& p, std::vector<BoltLine>& out) {
	out.clear();
	if (nodes.size() < 2) return 0;
	Rng rng(static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ULL + 0x5bd1e995u);
	BoltLine main;
	main.life = 1.0f;
	main.pts.push_back(nodes[0]);
	for (size_t i = 1; i < nodes.size(); ++i) Subdivide(main.pts, nodes[i - 1], nodes[i], p.jitter, 0, p, rng);
	const float total = PathLength(main.pts);
	main.width.resize(main.pts.size());
	for (size_t i = 0; i < main.pts.size(); ++i) main.width[i] = p.width * (0.85f + 0.3f * rng.F01());
	const int nb = rng.RangeI(p.minBranches, MaxI(p.minBranches, p.maxBranches));
	const Vec3 mainDir = Norm(nodes.back() - nodes.front(), Vec3(0.0f, 0.0f, 1.0f));
	out.push_back(main);
	for (int b = 0; b < nb && main.pts.size() >= 3; ++b) {
		// fork from the middle 70 % of the bolt
		const size_t at = static_cast<size_t>(Clamp(rng.Range(0.15f, 0.85f), 0.0f, 1.0f) * static_cast<float>(main.pts.size() - 1));
		const Vec3 from = main.pts[at];
		const Vec3 local = Norm(main.pts[std::min(at + 1, main.pts.size() - 1)] - main.pts[at > 0 ? at - 1 : 0], mainDir);
		const Vec3 dir = rng.InCone(Norm(local + mainDir), p.branchAngle);
		const float len = total * p.branchLen * rng.Range(0.5f, 1.0f);
		const Vec3 to = from + dir * len;
		BoltLine br;
		br.life = rng.Range(0.25f, 0.6f);
		br.pts.push_back(from);
		BoltParams bp = p;
		bp.maxLevels = MaxI(2, p.maxLevels - 2);
		Subdivide(br.pts, from, to, p.jitter * 1.2f, 0, bp, rng);
		br.width.resize(br.pts.size());
		for (size_t i = 0; i < br.pts.size(); ++i) {
			const float t = br.pts.size() > 1 ? static_cast<float>(i) / static_cast<float>(br.pts.size() - 1) : 0.0f;
			br.width[i] = p.width * p.branchWidth * (1.0f - 0.85f * t);
		}
		out.push_back(br);
	}
	int count = 0;
	for (const BoltLine& l : out) count += static_cast<int>(l.pts.size());
	return count;
}

void BuildBoltMesh(MeshData& m, const std::vector<BoltLine>& lines, const Vec3& camPos, float widthScale) {
	uint32_t strand = 0;
	for (const BoltLine& l : lines) {
		if (l.pts.size() < 2) continue;
		std::vector<float> w(l.width.size());
		for (size_t i = 0; i < w.size(); ++i) w[i] = l.width[i] * widthScale;
		std::vector<Color> cols(l.pts.size());
		for (size_t i = 0; i < cols.size(); ++i) cols[i] = Color(1.0f, l.life, 1.0f, 0.5f + 0.5f * Hash01(strand * 131u + static_cast<uint32_t>(i)));
		const int first = m.NumVerts();
		AppendRibbon(m, l.pts, w, camPos, cols, Hash01(strand + 7u));
		// uv1.y carries the line life for the shader (branches die early)
		for (int v = first; v < m.NumVerts(); ++v) m.uv1[static_cast<size_t>(v)].y = l.life;
		++strand;
	}
}

}  // namespace ffx
