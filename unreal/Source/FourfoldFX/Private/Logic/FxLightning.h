// FourfoldFX logic island - branching lightning generator (port + upgrade of LightningArcFX.strike).
// The main bolt passes EXACTLY through the given nodes (hand, conductors, target): every span is subdivided by
// recursive midpoint displacement perpendicular to the span (jitter proportional to the span length, halving per
// level), so the bolt looks fractal at any length. 1-3 tapering branches fork off the main bolt with the same
// generator. Re-strikes regenerate the jitter with a new seed while the nodes stay pinned. Deterministic per seed.
// Owner: stream `fx`.
#pragma once

#include "FxBase.h"
#include "FxMesh.h"

#include <cstdint>
#include <vector>

namespace ffx {

struct BoltParams {
	float jitter = 0.16f;        // displacement of a span midpoint, fraction of the span length (level 0)
	float roughness = 0.55f;     // displacement ratio per level
	float maxSegment = 0.18f;    // subdivide spans until segments are shorter than this (m)
	int maxLevels = 7;
	int minBranches = 1;
	int maxBranches = 3;
	float branchLen = 0.35f;     // branch length as a fraction of the main bolt length (random 0.5x .. 1x)
	float branchAngle = 0.6f;    // radians away from the main direction
	float width = 0.07f;         // main bolt glow width (m)
	float branchWidth = 0.55f;   // relative to the main bolt
};

struct BoltLine {
	std::vector<Vec3> pts;
	std::vector<float> width;    // full width per point (tapers on branches)
	float life = 1.0f;           // 1 = main bolt; < 1 branches die earlier (shader: COLOR.g)
};

// Generates the bolt into `out` (cleared). Returns the total number of points.
int GenerateBolt(const std::vector<Vec3>& nodes, uint32_t seed, const BoltParams& p, std::vector<BoltLine>& out);

// Builds camera-facing ribbons for the lines: uv0 = (across 0..1, metres along), uv1 = (along 0..1, life),
// col = (1, 1, 1, brightness variation).
void BuildBoltMesh(MeshData& m, const std::vector<BoltLine>& lines, const Vec3& camPos, float widthScale = 1.0f);

}  // namespace ffx
