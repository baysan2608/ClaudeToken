// Open-world water layer (ff/OpenWorld.h WorldDef::water_h, added 2026-10-14 for the Jungle Ruins map): sloped water
// surfaces per heightfield sample, the wading floor that follows them, the layer replacing the single lake level, the
// per-map edge margin, and (Task 2) wading, drawing and the static-terrain switch in a session. Unreal-only feature.
#include "ff_test.h"

#include "sim_harness.h"

#include "App/SessionCore.h"
#include "Sim/ArenaMap.h"
#include "Sim/CombatWorld.h"
#include "Sim/Conduction.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
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

namespace {
constexpr float kHalfPi = 1.5707963f;

struct CreekFx : fft::TestCase {
	InputFrame in;
	float yaw = 3.14159265f;   // camera along -z: from the bank (z 12) toward the creek

	void step(Session& s, int n = 1) {
		for (int i = 0; i < n; ++i) {
			s.Step(in, yaw);
			in.ClearEdges();
		}
		std::vector<Event> drop;
		s.TakeEvents(drop);
	}
	const ActorView* player(const Session& s) const { return s.GetSnapshot().FindActor(s.GetSnapshot().player_id); }
	double world_x(const Session& s) const { return s.GetSnapshot().origin_x + player(s)->pos.x; }
	double world_y(const Session& s) const { return s.GetSnapshot().origin_y + player(s)->pos.y; }
	double world_z(const Session& s) const { return s.GetSnapshot().origin_z + player(s)->pos.z; }
};
}  // namespace

FF_TEST_F(test_world_water, CreekFx, test_wading_follows_the_sloped_creek_and_walks_back_out) {
	Session s;
	RoamOptions o;
	o.kit = "all";
	o.encounters = false;
	check(s.LoadRoam(make_creek_world(), o), "creek world loads");
	in.move = Vec2(0.0f, 1.0f);
	for (int t = 0; t < 60 * 6 && !player(s)->in_water; ++t) step(s);
	check(player(s)->in_water, S("reached the water (z ", world_z(s), ")"));
	for (int t = 0; t < 60 * 3 && std::fabs(world_z(s)) > 3.0; ++t) step(s);   // on into the bed
	in.move = Vec2();
	step(s, 30);
	const double x0 = world_x(s), y0 = world_y(s);
	check(player(s)->in_water && std::fabs(world_z(s)) <= 4.0, S("standing in the bed (z ", world_z(s), ")"));
	near(y0, 0.05 * x0 - 1.15, 0.12, "feet on the wading floor (surface - wade depth)");
	yaw = -kHalfPi;            // camera along -x: downstream
	in.move = Vec2(0.0f, 1.0f);
	step(s, 60 * 2);
	in.move = Vec2();
	step(s, 30);
	const double x1 = world_x(s), y1 = world_y(s);
	check(x1 < x0 - 4.0, S("walked downstream (dx ", x1 - x0, ")"));
	check(player(s)->in_water, "still in the creek");
	near(y1 - y0, 0.05 * (x1 - x0), 0.15, "the wading floor followed the sloped surface");
	yaw = 3.14159265f;         // on across to the far bank (-z)
	in.move = Vec2(0.0f, 1.0f);
	step(s, 60 * 3);
	check(!player(s)->in_water && world_z(s) < -6.0, S("walked out onto the far bank (z ", world_z(s), ")"));
}

FF_TEST(test_world_water, test_draw_takes_water_from_the_creek_surface) {
	auto wd = make_creek_world();
	check(wd != nullptr, "creek world parses");
	if (!wd) return;
	fft::SimHarness h(5);
	// Bubble origin on the bank at world (0, 0, 14): the window's pool level is the surface nearest its centre (x 0: -0.3).
	// The fighter stands at local (10, -6) = world (10, 8), uphill: the creek edge is ~3.5 m ahead and its surface there
	// (0.2) is 0.5 m above the window's pool level, so a draw point taken from pool_level would be off the water.
	h.w->arena = ArenaMap::make_window(wd, 0.0, wd->FloorAt(0.0, 14.0), 14.0);
	const ArenaMap& m = h.w->arena;
	check(m.has_water_layer(), "the window carries the layer");
	near(m.pool_level, -0.3, 1e-5, "window pool level = the surface nearest the centre");
	MatBody* pool = h.w->pool;
	pool->pos = V3((m.pool_min.x + m.pool_max.x) * 0.5, m.pool_level, (m.pool_min.y + m.pool_max.y) * 0.5);
	ActorState* wf = h.actor("W", V3(10, 0.5, -6), 0, Dict(), Sim::WATER);
	h.step(10);
	Vec3 at;
	check(m.nearest_water(wf->pos.x, wf->pos.z, 7.5, at), "the creek is within reach");
	near(at.y, wd->WaterAt(at.x, at.z + 14.0), 1e-4, "the nearest point sits on the local surface");
	const double pool0 = pool->mass;
	h.press(wf, "tech");
	h.step(30);
	MatBody* held = h.w->held(*wf);
	check(held != nullptr && held->is_water(), "W holds drawn water");
	if (held != nullptr) near(pool0 - pool->mass, held->mass, 1e-9, "the creek paid exactly what W holds");
	check(h.any_event("draw_water", [&](const Dict& d) {
		const Vec3 p = d.get("at").as_vec3();
		return p.z + 14.0 <= 5.5 && p.x > 6.0 && std::fabs(p.y - wd->WaterAt(p.x, p.z + 14.0)) < 1e-4;
	}), "draw_water comes from a point on the creek surface in front of W (not the window's pool level)");
}

FF_TEST(test_world_water, test_lightning_meets_the_local_surface) {
	auto wd = make_creek_world();
	check(wd != nullptr, "creek world parses");
	if (!wd) return;
	fft::SimHarness h(5);
	h.w->arena = ArenaMap::make_window(wd, 0.0, wd->FloorAt(0.0, 14.0), 14.0);
	// world (10, 2) = local (10, -12): creek surface 0.2, the window's pool level -0.3
	near(h.w->arena.water_level_at(10.0, -12.0), 0.2, 1e-5, "water_level_at follows the layer");
	check(Conduction::surface_node_at(*h.w, V3(10.0, 0.35, -12.0)) == "pool", "a point 0.15 m above the local surface touches the water");
	check(Conduction::surface_node_at(*h.w, V3(10.0, 0.6, -12.0)).empty(), "0.4 m above it does not");
	check(Conduction::surface_node_at(*h.w, V3(-10.0, -0.6, -12.0)) == "pool", "downstream (surface -0.8) the same rule");
}

FF_TEST(test_world_water, test_static_map_keeps_its_terrain) {
	RoamOptions o;
	o.kit = "all";
	o.encounters = false;
	SessionCore g;
	g.load_roam(make_creek_world(", \"editable\": false"), o);
	check(g.roam != nullptr && !g.roam->editable_terrain, "terrain.editable false parsed");
	check(g.world->terrain_live == nullptr, "no live terrain: the ridge technique raises stone walls instead");
	SessionCore legacy;
	legacy.load_roam(make_creek_world(), o);
	check(legacy.world->terrain_live != nullptr, "default worlds keep live terrain edits");
}

FF_TEST(test_world_water, test_creek_roam_is_deterministic) {
	auto run = [] {
		Session s;
		RoamOptions o;
		o.kit = "all";
		o.encounters = false;
		s.LoadRoam(make_creek_world(), o);
		InputFrame f;
		for (int t = 0; t < 60 * 10; ++t) {
			const float a = 0.004f * static_cast<float>(t);
			f.move = Vec2(std::sin(a), std::cos(a));
			s.Step(f, 3.14159265f);
			f.ClearEdges();
		}
		std::vector<Event> ev;
		s.TakeEvents(ev);
		const ActorView* p = s.GetSnapshot().FindActor(s.GetSnapshot().player_id);
		return S(s.GetSnapshot().origin_x, ",", s.GetSnapshot().origin_z, ",", p->pos, ",", p->in_water, ",",
		         static_cast<long long>(ev.size()));
	};
	const std::string a = run();
	const std::string b = run();
	check(a == b, "two runs match: " + a + " vs " + b);
}
