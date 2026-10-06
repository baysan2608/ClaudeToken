// Fourfold game logic island - analytic arena queries (see FFGArena.h).
#include "FFGArena.h"

#include <utility>

namespace ffg {

void ArenaGround::Set(const ff::ArenaView& a) {
	half_size = a.half_size;
	solids.clear();
	const float h = a.half_size - 0.01f;
	for (const ff::ArenaBox& b : a.solids) {
		ArenaSolid s;
		s.mn = b.min;
		s.mx = b.max;
		s.name = b.name;
		s.boundary = b.max.x <= -h || b.min.x >= h || b.max.z <= -h || b.min.z >= h;
		solids.push_back(s);
	}
	pool_min = a.pool_min;
	pool_max = a.pool_max;
	pool_floor = a.pool_floor;
	pool_level = a.pool_level;
	metal_min = a.metal_min;
	metal_max = a.metal_max;
	metal_top = a.metal_top;
	valid = true;
}

float ArenaGround::BaseFloor(float x, float z) const {
	if (InPool(x, z)) return pool_floor;
	if (OnMetal(x, z)) return metal_top;
	return 0.0f;
}

float ArenaGround::GroundHeight(float x, float z, float from_y, float step) const {
	float g = BaseFloor(x, z);
	for (const ArenaSolid& s : solids)
		if (x >= s.mn.x && x <= s.mx.x && z >= s.mn.z && z <= s.mx.z && s.mx.y <= from_y + step && s.mx.y > g) g = s.mx.y;
	return g;
}

float ArenaGround::Slab(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx) {
	float tmin = 0.0f;
	float tmax = 1.0f;
	for (int i = 0; i < 3; ++i) {
		const float oi = o[i];
		const float di = d[i];
		if (std::fabs(di) < 1e-9f) {
			if (oi < mn[i] || oi > mx[i]) return -1.0f;
		} else {
			float t1 = (mn[i] - oi) / di;
			float t2 = (mx[i] - oi) / di;
			if (t1 > t2) std::swap(t1, t2);
			tmin = std::max(tmin, t1);
			tmax = std::min(tmax, t2);
			if (tmin > tmax) return -1.0f;
		}
	}
	return tmin;
}

}  // namespace ffg
