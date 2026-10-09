// Open world ("roam", ff/OpenWorld.h): terrain parsing / sampling, the bubble that follows the player, encounters
// (engage, flee, win), the shrine respawn and determinism. Unreal-only feature: no Godot reference.
#include "ff_test.h"

#include "App/SessionCore.h"
#include "ff/FourfoldCore.h"
#include "ff/Json.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace ff;
using fft::S;

namespace {
constexpr float kHalfPi = 1.5707963f;

// 401 x 401 samples at 2 m (800 m square, x / z in -400..400): a 10 % slope rising to +x (y 50 at x 0), a lake (floor -2,
// surface -0.5) for z > 120, one ruin wall, an ore plate, two sites and a shrine.
std::shared_ptr<WorldDef> make_world(bool with_sites = true) {
	const int n = 401;
	std::vector<float> h(static_cast<size_t>(n) * n);
	for (int k = 0; k < n; ++k) {
		for (int i = 0; i < n; ++i) {
			const double x = -400.0 + i * 2.0;
			const double z = -400.0 + k * 2.0;
			float v = z > 120.0 ? -2.0f : static_cast<float>(50.0 + 0.1 * x);
			if (z < -150.0 && z > -250.0 && x > -60.0 && x < 60.0) v += static_cast<float>(1.5 * (-150.0 - z));   // a 56-degree cliff face
			h[static_cast<size_t>(k) * n + i] = v;
		}
	}
	std::string sites = with_sites ? R"([
		{"id": "s_fire", "name": "Ember Adept", "region": "fire", "pos": [0, 50, -60], "element": 2, "preset": "novice", "aggro_radius": 9},
		{"id": "s_far", "name": "Far Adept", "region": "air", "pos": [300, 80, 300], "element": 3}])"
	                                 : "[]";
	const std::string json = std::string(R"({
		"terrain": {"nx": 401, "nz": 401, "x0": -400, "z0": -400, "cell": 2, "heights": "heights.f32"},
		"water": {"level": -0.5, "wade_depth": 0.85},
		"solids": [{"min": [30, 40, -5], "max": [32, 60, 5], "kind": "wall", "name": "ruin_wall"}],
		"metal": [{"min": [-40, -40], "max": [-34, -34]}],
		"sites": )") + sites + R"(,
		"shrines": [{"id": "shrine_hub", "name": "Hub Shrine", "pos": [-20, -2, 10]}],
		"spawn": {"pos": [0, 0, 0], "facing": 0}
	})";
	auto w = std::make_shared<WorldDef>();
	std::string err;
	if (!WorldDef::Parse(json, h.data(), h.size() * sizeof(float), *w, &err)) return nullptr;
	return w;
}

struct RoamFx : fft::TestCase {
	std::vector<Event> log;
	InputFrame in;
	float yaw = kHalfPi;   // camera looks along +x

	void step(Session& s, int n = 1) {
		for (int i = 0; i < n; ++i) {
			s.Step(in, yaw);
			in.ClearEdges();
		}
		s.TakeEvents(log);
	}
	int count(const std::string& type, const std::string& state = "") const {
		int c = 0;
		for (const Event& e : log)
			if (e.type == type && (state.empty() || e.data.get("state").as_string() == state)) ++c;
		return c;
	}
	const ActorView* player(const Session& s) const { return s.GetSnapshot().FindActor(s.GetSnapshot().player_id); }
	double world_x(const Session& s) const { return s.GetSnapshot().origin_x + player(s)->pos.x; }
	double world_y(const Session& s) const { return s.GetSnapshot().origin_y + player(s)->pos.y; }
	double world_z(const Session& s) const { return s.GetSnapshot().origin_z + player(s)->pos.z; }
};
}  // namespace

FF_TEST(test_open_world, test_parse_and_bilinear_height) {
	auto w = make_world();
	check(w != nullptr, "world parses");
	if (!w) return;
	check(w->sites.size() == 2 && w->shrines.size() == 1 && w->solids.size() == 1 && w->metal.size() == 1, "lists parsed");
	near(w->HeightAt(13.0, 7.0), 51.3, 1e-5, "slope sampled between samples");
	near(w->HeightAt(5000.0, 0.0), 90.0, 1e-5, "clamped to the grid edge");
	check(w->IsWater(0.0, 200.0) && !w->IsWater(0.0, 0.0), "lake mask");
	near(w->FloorAt(0.0, 200.0), -1.35, 1e-5, "deep water floor clamps to the wade depth");
	check(w->OnMetal(-37.0, -37.0), "ore plate");
	WorldDef bad;
	std::vector<float> few(10);
	check(!WorldDef::Parse(R"({"terrain": {"nx": 401, "nz": 401}})", few.data(), few.size() * 4, bad), "size mismatch rejected");
}

FF_TEST_F(test_open_world, RoamFx, test_bubble_follows_the_player_down_the_slope) {
	Session s;
	RoamOptions o;
	o.kit = "all";
	check(s.LoadRoam(make_world(false), o), "roam loads");
	check(s.ScenarioId() == "roam", "scenario id roam");
	check(s.Arena().world != nullptr, "arena view carries the terrain");
	yaw = -kHalfPi;               // camera along -x: downhill, away from the ruin wall
	in.move = Vec2(0.0f, 1.0f);
	double max_local = 0.0;
	for (int t = 0; t < 60 * 12; ++t) {
		step(s);
		const ActorView* p = player(s);
		max_local = std::fmax(max_local, std::fabs(p->pos.x));
	}
	const double wx = world_x(s);
	check(wx < -40.0, S("walked downhill across many windows (x ", wx, ")"));
	check(count("app_recenter") >= 4, S("re-centred ", count("app_recenter"), " times"));
	check(max_local < kRoamHalfSize - 2.0, S("stayed inside the window (max local x ", max_local, ")"));
	near(world_y(s), 50.0 + 0.1 * wx, 0.15, "feet on the terrain");
	near(s.Roam().origin_x, s.GetSnapshot().origin_x, 1e-9, "Roam() origin matches the snapshot");
}

FF_TEST_F(test_open_world, RoamFx, test_ruin_wall_blocks_and_lake_wets) {
	Session s;
	RoamOptions o;
	o.kit = "all";
	o.encounters = false;
	s.LoadRoam(make_world(false), o);
	in.move = Vec2(0.0f, 1.0f);
	step(s, 60 * 10);   // 50 m of running toward the wall at x 30..32
	check(world_x(s) < 30.0, S("wall stops the run (x ", world_x(s), ")"));
	// Turn to +z (camera along +z) and walk into the lake.
	yaw = 0.0f;
	step(s, 60 * 30);
	const ActorView* p = player(s);
	check(world_z(s) > 125.0, S("reached the lake (z ", world_z(s), ")"));
	check(p->in_water, "standing in the lake");
	check(world_y(s) > -1.4 && world_y(s) < -0.5, S("wading on the clamped floor (y ", world_y(s), ")"));
}

FF_TEST_F(test_open_world, RoamFx, test_encounter_engages_then_flees_at_the_edge) {
	Session s;
	RoamOptions o;
	o.kit = "all";
	s.LoadRoam(make_world(true), o);
	yaw = 3.14159265f;   // camera along -z, toward the fire site at z -60
	in.move = Vec2(0.0f, 1.0f);
	for (int t = 0; t < 60 * 20 && count("app_encounter", "engaged") == 0; ++t) step(s);
	check(count("app_encounter", "engaged") == 1, "the site engages");
	check(s.GetSnapshot().actors.size() == 2, "rival spawned in the bubble");
	check(s.Roam().engaged == 0 && s.Roam().sites[0].state == "engaged", "Roam view shows the fight");
	const int rec = count("app_recenter");
	// Turn and run away: the window stays put during the fight, the edge breaks it off.
	yaw = 0.0f;
	for (int t = 0; t < 60 * 20 && count("app_encounter", "fled") == 0; ++t) step(s);
	check(count("app_encounter", "fled") == 1, "running to the edge flees");
	check(s.GetSnapshot().actors.size() == 1, "rival removed");
	check(count("app_recenter") >= rec, "bubble resumes following");
	check(s.Roam().sites[0].state == "waiting", "site waits again");
	// Not re-armed while still near: walking back in the ring right away does not re-trigger immediately.
	in.move = Vec2();
	step(s, 30);
	check(count("app_encounter", "engaged") == 1, "no instant re-engage");
}

FF_TEST(test_open_world, test_win_and_respawn_through_the_session_core) {
	SessionCore g;
	RoamOptions o;
	o.kit = "all";
	g.load_roam(make_world(true), o);
	check(g.roam != nullptr && g.player != nullptr, "loaded");
	// Teleport next to the site and let it engage.
	g.player->pos = Vec3(0.0f, 0.0f, -54.0f);
	g.roam_tick();
	check(g.engaged == 0 && g.opponent != nullptr, "engaged");
	g.opponent->health = 0.0;
	InputFrame f;
	for (int t = 0; t < 60 * 4 && g.engaged >= 0; ++t) g.step(f, 0.0f);
	check(g.engaged < 0 && g.opponent == nullptr, "rival removed after the knockdown");
	check(g.site_state[0] == 2, "site defeated");
	check(g.world->actors.size() == 1, "one fighter left");
	bool won = false;
	for (const Event& e : g.events)
		if (e.type == "app_encounter" && e.data.get("state").as_string() == "won") won = true;
	check(won, "won event");
	// Lose at the far site: respawn at the shrine with full health.
	g.player->pos = Vec3(static_cast<float>(300.0 - g.origin_x), static_cast<float>(80.0 - g.origin_y), static_cast<float>(300.0 - g.origin_z));
	g.recentre(300.0, 300.0);
	g.player->pos = Vec3(0.0f, g.player->pos.y, 0.0f);
	g.roam_tick();
	check(g.engaged == 1, "far site engages");
	g.player->health = 0.0;
	for (int t = 0; t < 60 * 4 && g.engaged >= 0; ++t) g.step(f, 0.0f);
	check(g.engaged < 0, "fight over");
	check(g.site_state[1] == 0, "lost: the site waits again");
	near(g.player->pos.x + g.origin_x, -20.0, 0.01, "respawned at the shrine (x)");
	near(g.player->pos.z + g.origin_z, 10.0, 0.01, "respawned at the shrine (z)");
	near(g.player->health, Sim::HEALTH_MAX, 1e-9, "full health");
}

FF_TEST_F(test_open_world, RoamFx, test_roam_is_deterministic) {
	auto run = [&](uint64_t) {
		Session s;
		RoamOptions o;
		o.kit = "all";
		o.autoplay = "soak";
		s.LoadRoam(make_world(true), o);
		std::vector<Event> ev;
		InputFrame f;
		for (int t = 0; t < 60 * 30; ++t) s.Step(f, 0.3f);
		s.TakeEvents(ev);
		const ActorView* p = s.GetSnapshot().FindActor(s.GetSnapshot().player_id);
		return S(s.GetSnapshot().origin_x, ",", s.GetSnapshot().origin_z, ",", p->pos, ",", static_cast<long long>(ev.size()));
	};
	const std::string a = run(1);
	const std::string b = run(1);
	check(a == b, "two runs match: " + a + " vs " + b);
}

FF_TEST_F(test_open_world, RoamFx, test_steep_cliff_blocks_and_slides) {
	Session s;
	RoamOptions o;
	o.kit = "all";
	o.encounters = false;
	s.LoadRoam(make_world(false), o);
	yaw = 3.14159265f;   // camera along -z, into the cliff that starts at z -150
	in.move = Vec2(0.0f, 1.0f);
	step(s, 60 * 40);
	check(world_z(s) > -152.0, S("cliff stops the climb (z ", world_z(s), ")"));
	near(world_y(s), 50.0 + 0.1 * world_x(s), 1.0, "still at the foot");
	// Diagonal into the cliff: slides sideways along it.
	const double x0 = world_x(s);
	in.move = Vec2(0.7f, 0.7f);
	step(s, 60 * 2);
	check(std::fabs(world_x(s) - x0) > 3.0, S("slid along the face (dx ", world_x(s) - x0, ")"));
}
