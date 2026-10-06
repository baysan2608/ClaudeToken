// FourfoldFX logic island - arena ground queries. Owner: stream `fx`.
#include "FxArena.h"

#include "ff/Snapshot.h"

namespace ffx {

float GroundHeight(const ff::ArenaView* arena, float x, float z, float fromY) {
	if (!arena) return 0.0f;
	const ff::ArenaView& a = *arena;
	float g = 0.0f;
	if (x > a.pool_min.x && x < a.pool_max.x && z > a.pool_min.y && z < a.pool_max.y)
		g = a.pool_floor;
	else if (x > a.metal_min.x && x < a.metal_max.x && z > a.metal_min.y && z < a.metal_max.y)
		g = a.metal_top;
	for (const ff::ArenaBox& s : a.solids) {
		if (x >= s.min.x && x <= s.max.x && z >= s.min.z && z <= s.max.z)
			if (s.max.y <= fromY + kStepHeight && s.max.y > g) g = s.max.y;
	}
	return g;
}

}  // namespace ffx
