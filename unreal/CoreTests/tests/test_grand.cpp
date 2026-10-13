// Grand techniques (Combat/Grand.h): the surge meter, Firestorm (fire) and Upheaval (earth) in the Lab arena and on
// the open-world terrain; energy and mass ledgers stay exact. Unreal-only feature: no Godot reference.
#include "ff_test.h"

#include "App/SessionCore.h"
#include "ff/FourfoldCore.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace ff;
using fft::S;

namespace {
std::unique_ptr<SessionCore> lab_game(int element) {
	auto g = std::make_unique<SessionCore>();
	g->seed = 1;
	g->load_scenario("lab");
	g->lab.ai_enabled = false;
	g->player->element = element;
	g->events.clear();
	return g;
}

ActorState* rival(SessionCore& g) {
	for (const auto& a : g.world->actors)
		if (a.get() != g.player && !a->is_dummy) return a.get();
	return nullptr;
}

int count_events(const SessionCore& g, const std::string& type, const std::string& kind = "") {
	int n = 0;
	for (const Event& e : g.events)
		if (e.type == type && (kind.empty() || e.data.get("kind").as_string() == kind)) ++n;
	return n;
}

void tick_game(SessionCore& g, int ticks, bool grand_first = false) {
	InputFrame f;
	for (int t = 0; t < ticks; ++t) {
		f.grand_pressed = grand_first && t == 0;
		g.step(f, 3.14159265f);
	}
}
}  // namespace

FF_TEST(test_grand, test_surge_fills_and_gates_the_grand_press) {
	auto g = lab_game(2);
	tick_game(*g, 60, true);
	check(count_events(*g, "grand") == 0, "an empty meter does nothing");
	tick_game(*g, 60 * 13);
	near(g->player->surge, 100.0, 1e-6, "full after ~12.5 s");
}

FF_TEST(test_grand, test_firestorm_rolls_out_hits_and_keeps_the_energy_ledger) {
	auto g = lab_game(2);
	g->player->surge = 100.0;
	ActorState* r = rival(*g);
	check(r != nullptr, "lab rival");
	const double e0 = g->world->system_energy() - g->world->ledger_balance();
	const double hp0 = r->health;
	tick_game(*g, 1, true);
	check(g->player->action != nullptr && g->player->action->id == "grand_firestorm", "firestorm started");
	check(g->player->surge < 1.0, "meter spent");
	tick_game(*g, 40);
	int lines = 0;
	for (MatBody* b : g->world->body_list())
		if (b->alive && b->tag == "fire_line") ++lines;
	check(lines >= 3, S("a fan of flame walls (", lines, ")"));
	tick_game(*g, 120);
	check(r->health < hp0 - 10.0, S("the rival 14 m away was caught (hp ", r->health, ")"));
	near(g->world->system_energy() - g->world->ledger_balance(), e0, 1e-4, "energy ledger exact");
}

FF_TEST(test_grand, test_upheaval_raises_stone_throws_the_rival_and_keeps_the_mass_ledger) {
	auto g = lab_game(0);
	g->player->surge = 100.0;
	ActorState* r = rival(*g);
	g->player->lock_target = r->id;
	const double m0 = g->world->earth_mass();
	tick_game(*g, 1, true);
	check(g->player->action != nullptr && g->player->action->id == "grand_upheaval", "upheaval started");
	bool thrown = false;
	for (int t = 0; t < 90; ++t) {
		tick_game(*g, 1);
		thrown = thrown || r->vel.y > 4.0f;
	}
	int walls = 0;
	for (MatBody* b : g->world->body_list())
		if (b->alive && b->tag == "grand_ridge") ++walls;
	check(walls == 6, S("six ridge walls stand (", walls, ")"));
	if (!thrown) {
		std::string ev;
		for (const Event& e : g->events)
			if (e.type == "hit" || e.type == "grand") ev += e.type + ":" + e.data.to_string().substr(0, 160) + " | ";
		note(S("rival at ", r->pos, " player ", g->player->pos, " lock ", g->player->lock_target, " ev ", ev));
	}
	check(thrown, "the rival on the line was thrown up");
	near(g->world->earth_mass(), m0, 1e-6, "mass ledger: stone came from the ground");
	check(count_events(*g, "grand", "upheaval") >= 7, "gather + six segment events");
}

FF_TEST(test_grand, test_upheaval_reshapes_the_open_world_terrain) {
	const int n = 201;
	std::vector<float> h(static_cast<size_t>(n) * n, 10.0f);
	auto w = std::make_shared<WorldDef>();
	const std::string json = R"({"terrain": {"nx": 201, "nz": 201, "x0": -200, "z0": -200, "cell": 2},
		"water": {"level": -50}, "sites": [], "shrines": [], "spawn": {"pos": [0, 10, 0], "facing": 3.14159}})";
	check(WorldDef::Parse(json, h.data(), h.size() * 4, *w), "flat world parses");
	SessionCore g;
	RoamOptions o;
	o.kit = "all";
	o.encounters = false;
	g.load_roam(w, o);
	g.player->element = 0;
	g.player->facing = 3.14159265;
	g.player->surge = 100.0;
	tick_game(g, 1, true);
	tick_game(g, 100);
	const WorldDef& live = *g.roam_live;
	// facing pi = along -z from the spawn: the ridge runs ~2.5 .. 18.5 m out
	const double crest = live.HeightAt(0.0, -10.0);
	check(crest > 12.0, S("the ground rose into a ridge (", crest, ")"));
	check(live.HeightAt(0.0, 6.0) < 10.05 && live.HeightAt(0.0, 6.0) > 9.95, "behind the caster untouched");
	double sum = 0.0;
	for (float d : live.edits->delta) sum += d;
	near(sum * live.cell * live.cell, 0.0, 0.5, "cut and fill: no earth from nowhere");
	check(g.world->body_list().size() <= 2, "no stone walls in the open world (terrain itself moved)");
}
