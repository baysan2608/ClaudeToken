// Open-world water layer (ff/OpenWorld.h WorldDef::water_h, added 2026-10-14 for the Jungle Ruins map): sloped water
// surfaces per heightfield sample, the wading floor that follows them, the layer replacing the single lake level, the
// per-map edge margin, and (Task 2) wading, drawing and the static-terrain switch in a session. Unreal-only feature.
#include "ff_test.h"

#include "App/SessionCore.h"
#include "Sim/ArenaMap.h"
#include "ff/FourfoldCore.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace ff;
using fft::S;

namespace {
// 101 x 101 samples at 2 m (x / z in -100..100). The ground rises 5 % toward +x; a creek runs along x at z = 0: its bed
// lies 1.2 m below the banks for |z| <= 4 (the banks start at |z| = 6). The water layer holds a sloped surface 0.3 m
// below the banks for |z| <= 4 (NaN elsewhere). A dry hollow at x 60..70, z 30..40 is 5 m deep - below the creek's
// surface there - and world.json also names a global lake level of 1.0 that the layer must override.
std::shared_ptr<WorldDef> make_creek_world(const std::string& terrain_extra = "", bool with_layer = true) {
	const int n = 101;
	std::vector<float> h(static_cast<size_t>(n) * n), water(static_cast<size_t>(n) * n);
	for (int k = 0; k < n; ++k) {
		for (int i = 0; i < n; ++i) {
			const double x = -100.0 + i * 2.0;
			const double z = -100.0 + k * 2.0;
			const double base = 0.05 * x;
			const bool bed = std::fabs(z) <= 4.0;
			double v = bed ? base - 1.2 : base;
			if (x >= 60.0 && x <= 70.0 && z >= 30.0 && z <= 40.0) v = -5.0;   // the dry hollow
			const size_t s = static_cast<size_t>(k) * n + static_cast<size_t>(i);
			h[s] = static_cast<float>(v);
			water[s] = bed ? static_cast<float>(base - 0.3) : std::numeric_limits<float>::quiet_NaN();
		}
	}
	const std::string json = std::string(R"({
		"terrain": {"nx": 101, "nz": 101, "x0": -100, "z0": -100, "cell": 2, "heights": "heights.f32")") + terrain_extra + R"(},
		"water": {"level": 1.0, "wade_depth": 0.85)" + (with_layer ? std::string(R"(, "layer": "water.f32")") : std::string()) + R"(},
		"solids": [], "metal": [], "sites": [],
		"shrines": [{"id": "shrine_bank", "name": "Bank Shrine", "pos": [0, 0, 12]}],
		"spawn": {"pos": [0, 0, 12], "facing": 3.14159265}
	})";
	auto w = std::make_shared<WorldDef>();
	std::string err;
	if (!WorldDef::Parse(json, h.data(), h.size() * sizeof(float), *w, &err)) return nullptr;
	if (with_layer && !w->LoadWaterLayer(water.data(), water.size() * sizeof(float), &err)) return nullptr;
	return w;
}
}  // namespace

FF_TEST(test_world_water, test_layer_parses_and_samples_a_sloped_surface) {
	auto w = make_creek_world();
	check(w != nullptr, "creek world parses");
	if (!w) return;
	check(w->has_water_layer() && w->water_layer == "water.f32", "layer loaded from water.layer");
	near(w->WaterAt(10.0, 0.0), 0.2, 1e-5, "surface at x 10");
	near(w->WaterAt(-40.0, 2.0), -2.3, 1e-5, "the surface slopes with the creek");
	check(std::isnan(w->WaterAt(10.0, 20.0)), "no water on the bank");
	check(w->IsWater(10.0, 0.0) && w->IsWater(10.0, 5.0), "creek bed and its shallow edge are water");
	check(!w->IsWater(10.0, 5.6) && !w->IsWater(10.0, 20.0), "the bank is dry");
	near(w->FloorAt(10.0, 0.0), 0.5 - 1.15, 1e-5, "wading floor = surface - wade depth");
	near(w->FloorAt(10.0, 5.0), 0.5 - 0.6, 1e-5, "shallow edge: the ground itself");
	near(w->FloorAt(10.0, 20.0), 0.5, 1e-5, "bank floor = terrain");
}

FF_TEST(test_world_water, test_the_layer_replaces_the_global_level) {
	auto w = make_creek_world();
	check(w != nullptr, "creek world parses");
	if (!w) return;
	check(!w->IsWater(65.0, 35.0), "the dry hollow is not a lake");
	near(w->FloorAt(65.0, 35.0), -5.0, 1e-5, "its floor is the terrain (no wade clamp)");
	auto legacy = make_creek_world("", false);
	check(legacy != nullptr && !legacy->has_water_layer(), "a world without a layer");
	if (!legacy) return;
	check(legacy->IsWater(65.0, 35.0), "without a layer the global level floods the hollow (unchanged rule)");
	near(legacy->FloorAt(65.0, 35.0), 1.0 - 0.85, 1e-5, "legacy wade clamp");
	check(std::isnan(legacy->WaterAt(0.0, 0.0)), "no layer: WaterAt is NaN");
}

FF_TEST(test_world_water, test_bad_layers_and_margins_are_rejected) {
	auto w = make_creek_world();
	check(w != nullptr, "creek world parses");
	if (!w) return;
	std::vector<float> few(10);
	std::string err;
	check(!w->LoadWaterLayer(few.data(), few.size() * sizeof(float), &err), "size mismatch rejected");
	check(err.find("water layer") != std::string::npos, S("error names the layer (", err, ")"));
	check(w->has_water_layer(), "a rejected blob keeps the loaded layer");
	check(make_creek_world(", \"edge_margin\": 120") == nullptr, "a margin wider than half the map is rejected");
	check(make_creek_world(", \"edge_margin\": -1") == nullptr, "a negative margin is rejected");
}

FF_TEST(test_world_water, test_edge_margin_comes_from_world_json) {
	auto narrow = make_creek_world(", \"edge_margin\": 6");
	auto wide = make_creek_world();
	check(narrow != nullptr && wide != nullptr, "worlds parse");
	if (!narrow || !wide) return;
	near(narrow->edge_margin, 6.0, 1e-12, "edge_margin parsed");
	near(wide->edge_margin, 24.0, 1e-12, "default margin is the generated valley's 24 m");
	// Window centred at world (80, *, 20): local x 11 = world 91, local x 15 = world 95 (past 100 - 6).
	const ArenaMap m = ArenaMap::make_window(narrow, 80.0, narrow->FloorAt(80.0, 20.0), 20.0);
	const Vec3 in = m.limit_terrain(Vec3(8.0f, 0.0f, 0.0f), Vec3(11.0f, 0.0f, 0.0f));
	near(in.x, 11.0, 1e-6, "outside the 6 m band: the move stands");
	const Vec3 out = m.limit_terrain(Vec3(11.0f, 0.0f, 0.0f), Vec3(15.0f, 0.0f, 0.0f));
	near(out.x, 11.0, 1e-6, "into the 6 m band: blocked");
	const ArenaMap mw = ArenaMap::make_window(wide, 80.0, wide->FloorAt(80.0, 20.0), 20.0);
	const Vec3 w24 = mw.limit_terrain(Vec3(-8.0f, 0.0f, 0.0f), Vec3(-3.0f, 0.0f, 0.0f));
	near(w24.x, -8.0, 1e-6, "24 m default: world 77 is already inside the band");
}
