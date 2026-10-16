// The shipped Jungle Ruins world (Content/Fourfold/Data/ruins, baked by unreal/Tools/ruins/bake_ruins.py; never edited by
// hand): it parses with its water layer, the spawn and every site stand on dry ground inside the edge band, and a roam
// session loads it as a static map. Unreal-only feature (no Godot reference).
#include "ff_test.h"

#include "App/SessionCore.h"
#include "ff/FourfoldCore.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace ff;
using fft::S;

namespace {
const std::string kRuinsDir = std::string(FF_DATA_DIR) + "/../../../Content/Fourfold/Data/ruins/";

bool read_file(const std::string& path, std::string& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	return true;
}

std::shared_ptr<WorldDef> load_ruins(std::string& err) {
	std::string json, heights, water;
	if (!read_file(kRuinsDir + "world.json", json) || !read_file(kRuinsDir + "heights.f32", heights) ||
	    !read_file(kRuinsDir + "water.f32", water)) {
		err = "missing files in " + kRuinsDir;
		return nullptr;
	}
	auto w = std::make_shared<WorldDef>();
	if (!WorldDef::Parse(json, heights.data(), heights.size(), *w, &err)) return nullptr;
	if (!w->LoadWaterLayer(water.data(), water.size(), &err)) return nullptr;
	return w;
}
}  // namespace

FF_TEST(test_ruins_data, test_shipped_ruins_world_is_playable) {
	std::string err;
	auto w = load_ruins(err);
	check(w != nullptr, "ruins world loads " + err);
	if (!w) return;
	check(w->nx == 145 && w->nz == 145 && std::fabs(w->cell - 2.0) < 1e-12, S("grid ", w->nx, " x ", w->nz, " @ ", w->cell));
	check(w->has_water_layer() && w->water_layer == "water.f32", "water layer present");
	check(!w->editable_terrain && std::fabs(w->edge_margin - 8.0) < 1e-12, "static map with an 8 m edge band");
	int wet = 0;
	for (float v : w->water_h)
		if (v == v) ++wet;
	check(wet > 50, S(wet, " wet samples (the river)"));
	auto inside = [&](double x, double z) {
		return x > w->x0 + w->edge_margin && x < w->x0 + w->SizeX() - w->edge_margin && z > w->z0 + w->edge_margin &&
		       z < w->z0 + w->SizeZ() - w->edge_margin;
	};
	check(inside(w->player_spawn.x, w->player_spawn.z) && !w->IsWater(w->player_spawn.x, w->player_spawn.z), "spawn dry, inside the band");
	near(w->player_spawn.y, w->FloorAt(w->player_spawn.x, w->player_spawn.z), 0.05, "spawn stands on the floor");
	check(w->sites.size() >= 2 && !w->shrines.empty(), S(w->sites.size(), " sites, ", w->shrines.size(), " shrines"));
	for (const EncounterSite& s : w->sites)
		check(inside(s.pos.x, s.pos.z) && !w->IsWater(s.pos.x, s.pos.z), "site " + s.id + " dry, inside the band");
	SessionCore g;
	RoamOptions o;
	o.kit = "all";
	g.load_roam(w, o);
	check(g.roam != nullptr && g.player != nullptr && g.world->terrain_live == nullptr, "a roam session loads the ruins as a static map");
}
