// FourfoldFX logic island - arena ground queries. Owner: stream `fx`.
#include "FxArena.h"

#include "ff/Snapshot.h"

#include <algorithm>

namespace ffx {

float GroundHeight(const ff::ArenaView* arena, float x, float z, float fromY) {
	if (!arena) return 0.0f;
	const ff::ArenaView& a = *arena;
	float g = 0.0f;
	if (a.world)
		g = a.TerrainAt(x, z);   // open world: lake floor / ore plates follow the terrain too
	else if (x > a.pool_min.x && x < a.pool_max.x && z > a.pool_min.y && z < a.pool_max.y)
		g = a.pool_floor;
	else if (x > a.metal_min.x && x < a.metal_max.x && z > a.metal_min.y && z < a.metal_max.y)
		g = a.metal_top;
	for (const ff::ArenaBox& s : a.solids) {
		if (x >= s.min.x && x <= s.max.x && z >= s.min.z && z <= s.max.z)
			if (s.max.y <= fromY + kStepHeight && s.max.y > g) g = s.max.y;
	}
	return g;
}

bool PointBehindFloor(const ff::ArenaView* arena, const Vec3& camPos, const Vec3& camFwd, float depth, float minDepth,
                      float maxDist, Vec3& out) {
	const float down = -camFwd.y;   // metres the ray drops per metre travelled
	if (down < 0.02f) return false;
	constexpr float kStep = 0.25f;
	for (float t = kStep; t <= maxDist; t += kStep) {
		const Vec3 p = camPos + camFwd * t;
		const float g = GroundUnder(arena, p);
		if (p.y > g) continue;
		const float tEnd = std::min(t + depth / down, maxDist);
		if ((tEnd - t) * down < minDepth) return false;
		out = camPos + camFwd * tEnd;
		return true;
	}
	return false;
}

}  // namespace ffx
