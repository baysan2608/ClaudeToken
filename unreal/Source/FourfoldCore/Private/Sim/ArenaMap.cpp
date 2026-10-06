// Fourfold core - port of game/core/arena_map.gd.
#include "Sim/ArenaMap.h"

#include "Util/GodotMath.h"

#include <cmath>

namespace ff {

ArenaMap ArenaMap::make_lab() {
	ArenaMap a;
	const float h = static_cast<float>(a.half_size);
	// Boundary walls
	a.add_box(Vec3(-h - 1, 0, -h - 1), Vec3(h + 1, 3.5f, -h), "wall", "stone", "north_wall");
	a.add_box(Vec3(-h - 1, 0, h), Vec3(h + 1, 3.5f, h + 1), "wall", "stone", "south_wall");
	a.add_box(Vec3(-h - 1, 0, -h), Vec3(-h, 3.5f, h), "wall", "stone", "west_wall");
	a.add_box(Vec3(h, 0, -h), Vec3(h + 1, 3.5f, h), "wall", "stone", "east_wall");
	// Low cover wall west of the duel line
	a.add_box(Vec3(-5.0f, 0, -1.25f), Vec3(-2.5f, 1.7f, -0.75f), "wall", "stone", "cover_wall");
	// Terrace behind the player
	a.add_box(Vec3(-4.0f, 0, 10.0f), Vec3(4.0f, 0.6f, 14.0f), "ledge", "stone", "terrace");
	// Walkable step block
	a.add_box(Vec3(9.0f, 0, 9.0f), Vec3(12.0f, 0.35f, 12.0f), "ledge", "stone", "step_block");
	// High ledge (updraft traversal)
	a.add_box(Vec3(-15.0f, 0, -15.0f), Vec3(-10.5f, 1.8f, -10.5f), "ledge", "stone", "high_ledge");
	// Pillars
	a.add_box(Vec3(12.5f, 0, -13.5f), Vec3(13.5f, 3.2f, -12.5f), "pillar", "stone", "pillar_ne");
	a.add_box(Vec3(-13.5f, 0, 12.5f), Vec3(-12.5f, 3.2f, 13.5f), "pillar", "stone", "pillar_sw");
	return a;
}

void ArenaMap::add_box(Vec3 mn, Vec3 mx, const std::string& kind, const std::string& surface, const std::string& nm) {
	solids.push_back(ArenaSolid{mn, mx, kind, surface, nm});
}

bool ArenaMap::in_pool(double x, double z) const { return x > pool_min.x && x < pool_max.x && z > pool_min.y && z < pool_max.y; }
bool ArenaMap::on_metal(double x, double z) const { return x > metal_min.x && x < metal_max.x && z > metal_min.y && z < metal_max.y; }

double ArenaMap::base_floor(double x, double z) const {
	if (in_pool(x, z)) return pool_floor;
	if (on_metal(x, z)) return metal_top;
	return 0.0;
}

double ArenaMap::ground_height(double x, double z, double from_y, double step) const {
	double g = base_floor(x, z);
	for (const ArenaSolid& s : solids) {
		if (x >= s.min.x && x <= s.max.x && z >= s.min.z && z <= s.max.z) {
			if (s.max.y <= from_y + step && s.max.y > g) g = s.max.y;
		}
	}
	return g;
}

std::string ArenaMap::surface_at(double x, double z, double y) const {
	if (in_pool(x, z) && y < pool_level + 0.15) return "water";
	if (on_metal(x, z) && std::fabs(y - metal_top) < 0.15) return "metal";
	return "stone";
}

Vec3 ArenaMap::push_out(Vec3 p, double r, double h, double step) const {
	Vec3 out = p;
	for (const ArenaSolid& s : solids) {
		const Vec3 mn = s.min;
		const Vec3 mx = s.max;
		if (mx.y <= out.y + step || mn.y >= out.y + h) continue;
		const double cx = clampf(out.x, mn.x, mx.x);
		const double cz = clampf(out.z, mn.z, mx.z);
		const double dx = out.x - cx;
		const double dz = out.z - cz;
		const double d2 = dx * dx + dz * dz;
		if (d2 >= r * r) continue;
		if (d2 > 1e-8) {
			const double d = std::sqrt(d2);
			out.x = f32(cx + dx / d * r);
			out.z = f32(cz + dz / d * r);
		} else {
			// Centre inside the box: leave through the nearest face.
			const double pen[4] = {out.x - mn.x, mx.x - out.x, out.z - mn.z, mx.z - out.z};
			int i = 0;
			for (int k = 1; k < 4; ++k)
				if (pen[k] < pen[i]) i = k;
			switch (i) {
				case 0: out.x = f32(mn.x - r); break;
				case 1: out.x = f32(mx.x + r); break;
				case 2: out.z = f32(mn.z - r); break;
				default: out.z = f32(mx.z + r); break;
			}
		}
	}
	return out;
}

double ArenaMap::segment_hit(Vec3 a, Vec3 b, double radius) const {
	double best = -1.0;
	const Vec3 d = b - a;
	const float rr = f32(radius);
	for (const ArenaSolid& s : solids) {
		const Vec3 mn = s.min - Vec3(rr, rr, rr);
		const Vec3 mx = s.max + Vec3(rr, rr, rr);
		const double t = slab(a, d, mn, mx);
		if (t >= 0.0 && (best < 0.0 || t < best)) best = t;
	}
	return best;
}

double ArenaMap::slab(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx) {
	double tmin = 0.0;
	double tmax = 1.0;
	for (int i = 0; i < 3; ++i) {
		const double oi = o[i];
		const double di = d[i];
		if (std::fabs(di) < 1e-9) {
			if (oi < mn[i] || oi > mx[i]) return -1.0;
		} else {
			double t1 = (mn[i] - oi) / di;
			double t2 = (mx[i] - oi) / di;
			if (t1 > t2) {
				const double tt = t1;
				t1 = t2;
				t2 = tt;
			}
			tmin = maxf(tmin, t1);
			tmax = minf(tmax, t2);
			if (tmin > tmax) return -1.0;
		}
	}
	return tmin;
}

const ArenaSolid* ArenaMap::solid_named(const std::string& nm) const {
	for (const ArenaSolid& s : solids)
		if (s.name == nm) return &s;
	return nullptr;
}

}  // namespace ff
