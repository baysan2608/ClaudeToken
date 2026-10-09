// Fourfold game logic island - analytic arena queries for presentation (port of game/core/arena_map.gd: ground height,
// segment / box slab test). Sim space: metres, +Y up. Used by the camera (collision) and the anim runtime (foot IK
// ground) on a copy of ff::ArenaView, so the anim worker thread never touches the session.
#pragma once

#include "FFGMath.h"
#include "ff/Snapshot.h"

#include <memory>
#include <string>
#include <vector>

namespace ffg {

struct ArenaSolid {
	Vec3 mn, mx;
	std::string name;
	bool boundary = false;   // one of the yard's outer walls
};

class ArenaGround {
public:
	static constexpr float kStepHeight = 0.4f;   // Sim.STEP_HEIGHT

	float half_size = 16.0f;
	std::vector<ArenaSolid> solids;
	Vec2 pool_min{7.0f, -5.0f}, pool_max{13.0f, 3.0f};
	float pool_floor = -0.3f, pool_level = -0.05f;
	Vec2 metal_min{-12.0f, -4.0f}, metal_max{-6.0f, 2.0f};
	float metal_top = 0.02f;
	bool valid = false;
	// Open world: terrain under the bubble and the world position of sim (0,0,0) (null / 0 in the arenas).
	std::shared_ptr<const ff::WorldDef> world;
	double ox = 0.0, oy = 0.0, oz = 0.0;
	int64_t revision = -1;

	void Set(const ff::ArenaView& a);
	bool InPool(float x, float z) const {
		if (!(x > pool_min.x && x < pool_max.x && z > pool_min.y && z < pool_max.y)) return false;
		return !world || world->IsWater(x + ox, z + oz);
	}
	// Terrain floor alone (0 in the arenas).
	float Terrain(float x, float z) const { return world ? float(world->FloorAt(x + ox, z + oz) - oy) : 0.0f; }
	// Ground under an Unreal world point given in metres (X = world x, Y = world z, up = world y): the anim runtime
	// samples in that frame, which includes the open-world bubble origin.
	float GroundHeightUEMetres(float x, float y, float from_up, float step = kStepHeight) const {
		return GroundHeight(float(x - ox), float(y - oz), float(from_up - oy), step) + float(oy);
	}
	bool OnMetal(float x, float z) const { return x > metal_min.x && x < metal_max.x && z > metal_min.y && z < metal_max.y; }
	float BaseFloor(float x, float z) const;
	// Highest walkable surface under (x, z) reachable from height from_y (surfaces above from_y + step are walls).
	float GroundHeight(float x, float z, float from_y = 1e9f, float step = kStepHeight) const;
	// First hit parameter t in [0, 1] of the segment o -> o + d against the box (mn, mx); -1 when it misses.
	static float Slab(Vec3 o, Vec3 d, Vec3 mn, Vec3 mx);
};

}  // namespace ffg
