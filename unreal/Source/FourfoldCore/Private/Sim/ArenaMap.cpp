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

ArenaMap ArenaMap::make_window(std::shared_ptr<const WorldDef> w, double x, double y, double z, double half) {
	ArenaMap a;
	a.half_size = half;
	a.world = std::move(w);
	a.ox = x;
	a.oy = y;
	a.oz = z;
	const WorldDef& wd = *a.world;
	const double reach = half + 2.0;
	for (const WorldBox& b : wd.solids) {
		if (b.max.x < x - reach || b.min.x > x + reach || b.max.z < z - reach || b.min.z > z + reach) continue;
		const Vec3 o(f32(x), f32(y), f32(z));
		a.add_box(b.min - o, b.max - o, b.kind, b.surface, b.name);
	}
	// Bounding rects of the lake / ore inside the window (empty rects far outside when there is none).
	double pmnx = 1.0e9, pmnz = 1.0e9, pmxx = -1.0e9, pmxz = -1.0e9;
	const double step = wd.cell > 0.0 ? wd.cell * 0.5 : 1.0;
	for (double lz = -half; lz <= half; lz += step) {
		for (double lx = -half; lx <= half; lx += step) {
			if (!wd.IsWater(x + lx, z + lz)) continue;
			pmnx = minf(pmnx, lx - step);
			pmnz = minf(pmnz, lz - step);
			pmxx = maxf(pmxx, lx + step);
			pmxz = maxf(pmxz, lz + step);
		}
	}
	if (pmxx > pmnx) {
		a.pool_min = Vec2(f32(pmnx), f32(pmnz));
		a.pool_max = Vec2(f32(pmxx), f32(pmxz));
	} else {
		a.pool_min = Vec2(1.0e6f, 1.0e6f);
		a.pool_max = Vec2(1.0e6f, 1.0e6f);
	}
	a.pool_level = wd.water_level - y;
	a.pool_floor = wd.water_level - wd.wade_depth - y;
	a.metal_min = Vec2(1.0e6f, 1.0e6f);
	a.metal_max = Vec2(1.0e6f, 1.0e6f);
	for (const WorldRect& r : wd.metal) {
		if (r.max.x < x - half || r.min.x > x + half || r.max.y < z - half || r.min.y > z + half) continue;
		a.metal_min = Vec2(f32(r.min.x - x), f32(r.min.y - z));
		a.metal_max = Vec2(f32(r.max.x - x), f32(r.max.y - z));
		break;
	}
	a.metal_top = 0.02;
	a.player_spawn = Vec3(0.0f, f32(wd.FloorAt(x, z) - y), 0.0f);
	a.opponent_spawn = a.player_spawn;
	return a;
}

void ArenaMap::add_box(Vec3 mn, Vec3 mx, const std::string& kind, const std::string& surface, const std::string& nm) {
	solids.push_back(ArenaSolid{mn, mx, kind, surface, nm});
}

bool ArenaMap::in_pool(double x, double z) const {
	if (!(x > pool_min.x && x < pool_max.x && z > pool_min.y && z < pool_max.y)) return false;
	return !world || world->IsWater(x + ox, z + oz);
}
bool ArenaMap::on_metal(double x, double z) const { return x > metal_min.x && x < metal_max.x && z > metal_min.y && z < metal_max.y; }

double ArenaMap::base_floor(double x, double z) const {
	if (world) {
		const double f = world->FloorAt(x + ox, z + oz) - oy;
		return on_metal(x, z) ? f + metal_top : f;
	}
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
	if (on_metal(x, z) && std::fabs(y - (world ? base_floor(x, z) : metal_top)) < 0.15) return "metal";
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

Vec3 ArenaMap::limit_terrain(Vec3 from, Vec3 to) const {
	if (!world) return to;
	const WorldDef& wd = *world;
	auto blocked = [&](double x, double z) {
		const double wx = x + ox;
		const double wz = z + oz;
		if (wx < wd.x0 + kWorldEdgeMargin || wx > wd.x0 + wd.SizeX() - kWorldEdgeMargin || wz < wd.z0 + kWorldEdgeMargin ||
		    wz > wd.z0 + wd.SizeZ() - kWorldEdgeMargin)
			return true;
		const double run = Vec2(f32(x - from.x), f32(z - from.z)).length();
		if (run < 1e-6) return false;
		const double rise = wd.FloorAt(wx, wz) - wd.FloorAt(from.x + ox, from.z + oz);
		return rise > 0.05 && rise > run * kMaxSlope;
	};
	Vec3 out = to;
	if (blocked(out.x, out.z)) {
		// Slide: keep whichever axis is still allowed.
		if (!blocked(to.x, from.z)) out.z = from.z;
		else if (!blocked(from.x, to.z)) out.x = from.x;
		else {
			out.x = from.x;
			out.z = from.z;
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
