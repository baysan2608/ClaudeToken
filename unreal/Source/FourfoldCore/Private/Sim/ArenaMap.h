// Fourfold core - port of game/core/arena_map.gd: analytic description of the combat laboratory (ground height,
// wall pushes, line of sight) so the sim needs no physics engine.
#pragma once

#include "ff/Math.h"
#include "Sim/Sim.h"

#include <string>
#include <vector>

namespace ff {

struct ArenaSolid {
	Vec3 min, max;
	std::string kind, surface, name;
};

class ArenaMap {
public:
	double half_size = 16.0;
	std::vector<ArenaSolid> solids;
	Vec2 pool_min{7.0f, -5.0f};
	Vec2 pool_max{13.0f, 3.0f};
	double pool_floor = -0.3;
	double pool_level = -0.05;
	Vec2 metal_min{-12.0f, -4.0f};
	Vec2 metal_max{-6.0f, 2.0f};
	double metal_top = 0.02;
	Vec3 player_spawn{0.0f, 0.0f, 7.0f};
	Vec3 opponent_spawn{0.0f, 0.0f, -7.0f};

	static ArenaMap make_lab();
	void add_box(Vec3 mn, Vec3 mx, const std::string& kind, const std::string& surface, const std::string& nm);
	bool in_pool(double x, double z) const;
	bool on_metal(double x, double z) const;
	double base_floor(double x, double z) const;
	// Highest walkable surface under (x, z) reachable from from_y (surfaces above from_y + step are walls).
	double ground_height(double x, double z, double from_y = 1.0e30, double step = Sim::STEP_HEIGHT) const;
	std::string surface_at(double x, double z, double y) const;
	Vec3 push_out(Vec3 p, double r, double h = Sim::ACTOR_HEIGHT, double step = Sim::STEP_HEIGHT) const;
	// First hit of segment a->b against solids (inflated by radius): t in 0..1, or -1.
	double segment_hit(Vec3 a, Vec3 b, double radius = 0.0) const;
	static double slab(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx);
	bool has_los(Vec3 a, Vec3 b) const { return segment_hit(a, b) < 0.0; }
	const ArenaSolid* solid_named(const std::string& nm) const;
};

}  // namespace ff
