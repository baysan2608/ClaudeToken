// Port of game/tests/sim/test_regressions_game.gd: regressions for the scenario rules that live in Game (game.gd), here
// ff::SessionCore (App/SessionCore.h): KOs (targets stand back up, a downed player or rival resets the round), the boulder
// launcher's seized plinth stone, Cold Hands vents re-melting set rock, the Storm's Path count and the water DRAW hint.
// Godot's GameProbe stubs load_scenario; here the real one runs and reloads are counted by their app_round_reset events.
// Not ported (presentation, owned by the game stream): adaptive quality stepping and the PerfMonitor session log.
#include "ff_test.h"
#include "sim_harness.h"

#include "App/SessionCore.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
BodyRef keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }

std::string f2(double v, int prec) {
	char b[64];
	std::snprintf(b, sizeof(b), "%.*f", prec, v);
	return b;
}

std::unique_ptr<SessionCore> make_game(const std::string& id) {
	auto g = std::make_unique<SessionCore>();
	g->seed = 1;
	g->load_scenario(id);   // Scenarios.build(id, progress, 1); launch_t = 1.5
	g->events.clear();
	return g;
}

ActorState* actor_named(SessionCore& g, const std::string& nm) {
	for (const auto& a : g.world->actors)
		if (a->name == nm) return a.get();
	return nullptr;
}

int reloads(const SessionCore& g) {
	int n = 0;
	for (const Event& e : g.events)
		if (e.type == "app_round_reset") n += 1;
	return n;
}

std::string last_toast(const SessionCore& g) {
	std::string t;
	for (const Event& e : g.events)
		if (e.type == "app_toast") t = e.data.get("text").to_string();
	return t;
}

// One Game tick without input devices: scenario props, sim step, KO handling.
std::vector<Dict> tick(SessionCore& g, ActorIntent& it) {
	g.scenario_tick();
	std::map<int, ActorIntent> intents;
	intents[g.player->id] = it;
	g.world->step(intents);
	g.ko_tick();
	std::vector<Dict> evs = g.world->take_events();
	it.attack_pressed = false;
	it.attack_released = false;
	it.tech_pressed = false;
	it.tech_released = false;
	it.element_select = -1;
	return evs;
}

Array bolt(SessionCore& g, ActorState* target) {
	ActorIntent it;
	g.player->lock_target = target->id;
	it.attack_pressed = true;
	it.attack_held = true;
	Array victims;
	for (int i = 0; i < 90; ++i) {
		if (i == 60) {
			it.attack_held = false;
			it.attack_released = true;
		}
		g.player->lock_target = target->id;
		const std::vector<Dict> evs = tick(g, it);
		for (const Dict& e : evs)
			if (dstr(e, "type") == "conduct" && dint(e, "actor", -1) == g.player->id) victims = darr(e, "victims");
		g.challenges(evs);
	}
	return victims;
}

bool water_hint(SessionCore& g, Vec3 pos, double facing) {
	g.player->pos = pos;
	g.player->facing = facing;
	g.player->lock_target = -1;
	const ActorIntent none;
	HudBuilder::Context c;
	c.world = g.world.get();
	c.player = g.player;
	c.player_intent = &none;
	return HudBuilder::build(c).tech_available;
}
}  // namespace

// ================================================================ KO

FF_TEST(test_regressions_game, test_downed_target_is_knocked_down_then_stands_at_full_health) {
	auto g = make_game("conduction");
	ActorState* d = actor_named(*g, "Target 1");
	if (!check(d != nullptr, "setup: the conduction scenario has Target 1")) return;
	ActorIntent it;
	d->health = 0.0;
	tick(*g, it);
	check(d->stun > 0.0 && d->stun_kind == "knockdown", S("a target at 0 HP goes down (stun ", f2(d->stun, 2), " ", d->stun_kind, ")"));
	for (int i = 0; i < 180; ++i) tick(*g, it);
	near(d->health, Sim::HEALTH_MAX, 0.01, "3 s later the target is back at full health");
	check(g->world->_valid_target(*g->player, d->id), "it can be targeted and hit again");
	check(reloads(*g) == 0, "a target going down never resets the round");
}

FF_TEST(test_regressions_game, test_downed_rival_resets_the_round_after_the_knockdown) {
	auto g = make_game("molten_exchange");
	ActorIntent it;
	g->opponent->health = 0.0;
	tick(*g, it);
	check(g->opponent->stun_kind == "knockdown", "the rival is knocked down");
	check(last_toast(*g) == "Rival down", S("toast says the rival is down ('", last_toast(*g), "')"));
	int reset_at = -1;
	bool stayed_down = true;
	for (int i = 0; i < 240; ++i) {
		g->scenario_tick();
		std::map<int, ActorIntent> intents;
		intents[g->player->id] = it;
		g->world->step(intents);
		if (g->opponent->stun_kind != "knockdown") stayed_down = false;   // read before the reload replaces the world
		if (g->ko_tick()) {
			reset_at = i;
			break;
		}
	}
	check(reloads(*g) == 1 && g->scenario_id == "molten_exchange", S("the round resets once (", reloads(*g), " resets)"));
	check(reset_at > 60 && reset_at < 180, S("after the knockdown has played (tick ", reset_at, ")"));
	check(stayed_down, "no getup before the reset");
}

FF_TEST(test_regressions_game, test_downed_player_resets_the_round_keeping_challenge_progress) {
	auto g = make_game("stone_rain");
	ActorIntent it;
	g->challenge_n = 2;
	g->player->health = 0.0;
	tick(*g, it);
	check(last_toast(*g) == "You're down", S("toast says you are down ('", last_toast(*g), "')"));
	for (int i = 0; i < 200; ++i) {
		tick(*g, it);
		if (reloads(*g) > 0) break;
	}
	check(reloads(*g) == 1, "the round resets");
	check(g->challenge_n == 2, S("challenge progress survives the reset (", g->challenge_n, ")"));
	const std::string& ct = g->challenge_text;
	check(ct.size() >= 3 && ct.compare(ct.size() - 3, 3, "2/3") == 0, S("HUD shows it ('", ct, "')"));
}

// ================================================================ boulder launcher

FF_TEST(test_regressions_game, test_seized_plinth_stone_is_released_by_the_launcher_and_flies_when_thrown) {
	auto g = make_game("boulder");
	ActorState* p = g->player;
	ActorIntent it;
	p->pos = V3(0, 0, -4.5);
	p->facing = kPi;
	it.element_select = Sim::EARTH;
	BodyRef b;
	for (int i = 0; i < 120; ++i) {
		tick(*g, it);
		b = keep(g->world->get_body(g->launch_body));
		if (b != nullptr) break;
	}
	if (!check(b != nullptr && b->static_body, "a stone sits static on the plinth during the telegraph")) return;
	it.tech_pressed = true;
	it.tech_held = true;
	int held = 0;
	for (int i = 0; i < 60; ++i) {
		tick(*g, it);
		if (b->controller == p->id) {
			held += 1;
			if (held > 18) break;
		}
	}
	check(b->controller == p->id, "the player seized the plinth stone");
	check(!b->static_body, "a seized stone is no longer static");
	check(g->launch_body == -1, "the launcher lets go of it");
	it.tech_held = false;
	it.tech_released = true;
	const Vec3 from = b->pos;
	std::set<int> owners;
	for (int i = 0; i < 30; ++i) {
		tick(*g, it);
		owners.insert(b->attack_owner);
	}
	check(b->pos.distance_to(from) > 3.0f, S("the thrown stone flies (moved ", f2(static_cast<double>(b->pos.distance_to(from)), 2), " m)"));
	std::string ow;
	for (int x : owners) ow += S(ow.empty() ? "" : ", ", x);
	check(owners.count(-1) == 0, "the launcher never fires the player's stone (owners [" + ow + "])");
}

// ================================================================ Cold Hands vents

FF_TEST(test_regressions_game, test_vent_does_not_remelt_lava_drawn_into_rock) {
	auto g = make_game("cold_hands");
	ActorState* p = g->player;
	CombatWorld& w = *g->world;
	ActorIntent it;
	BodyRef b;
	for (const BodyRef& c : w.bodies)
		if (c->alive && c->origin == "vent" && (b == nullptr || c->pos.distance_to(p->pos) < b->pos.distance_to(p->pos))) b = c;
	if (!check(b != nullptr, "setup: the scenario has a vent pool")) return;
	bool set_rock = false;
	for (int i = 0; i < 1200; ++i) {
		const Vec3 d = b->pos - p->pos;
		p->facing = std::atan2(static_cast<double>(d.x), static_cast<double>(d.z));
		it.tech_pressed = p->action == nullptr;
		it.tech_held = true;
		tick(*g, it);
		if (b->phase == Phase::Solid) {
			set_rock = true;
			break;
		}
	}
	if (!check(set_rock, "the drawn pool sets into rock")) return;
	// Released on the very tick it set: the leftover melt fraction is still just above 0.
	it.tech_held = false;
	it.tech_released = true;
	double peak = b->liquid;
	for (int i = 0; i < 600; ++i) {
		tick(*g, it);
		peak = maxf(peak, b->liquid);
	}
	check(b->alive && b->phase == Phase::Solid, S("the rock stays rock (", Sim::phase_name(b->phase), ")"));
	check(peak <= 0.05, S("the vent does not re-melt it (peak liquid ", f2(peak, 3), ")"));
}

// ================================================================ Storm's Path

FF_TEST(test_regressions_game, test_storm_path_does_not_count_the_caster_as_a_target) {
	auto g = make_game("conduction");
	Dict ch = ddict(g->scen_def, "challenge").duplicate();
	ch.set("count", 2);   // keep it from completing
	Dict sd = g->scen_def.duplicate();
	sd.set("challenge", ch);
	g->scen_def = sd;
	g->player->pos = V3(-5.6, 0, 2.0);
	ActorState* t5 = actor_named(*g, "Target 5");
	ActorState* t1 = actor_named(*g, "Target 1");
	if (!check(t5 != nullptr && t1 != nullptr, "setup: targets 1 and 5 exist")) return;
	Array victims = bolt(*g, t5);
	check(victims.has(g->player->id) && victims.size() == 2, S("the caster in the puddle is a conduction victim (", victims, ")"));
	check(g->challenge_n == 0, S("one target hit is not 'hit 2 targets' (count ", g->challenge_n, ")"));
	// Two targets in the pool, caster outside it: counts.
	g->player->pos = V3(2.0, 0, 1.0);
	g->player->focus = 100.0;
	victims = bolt(*g, t1);
	check(!victims.has(g->player->id) && victims.size() >= 2, S("both pool targets are reached (", victims, ")"));
	check(g->challenge_n == 1, S("two targets in one bolt count (count ", g->challenge_n, ")"));
}

// ================================================================ water DRAW hint

FF_TEST(test_regressions_game, test_water_draw_hint_follows_the_real_draw_sources) {
	{
		auto g = make_game("water_ice");
		g->player->element = Sim::WATER;
		g->player->water_carried = 0.0;   // empty waterskin: only the pool / puddles can supply
		check(water_hint(*g, V3(0, 0, -1), 0.0), "pool edge 7.0 m away (reach 7.5): DRAW works, hint lit");
		check(!water_hint(*g, V3(-3, 0, -1), 0.0), "10 m from the pool, no puddle: hint dimmed");
		g->player->water_carried = 0.7;
		check(!water_hint(*g, V3(-3, 0, -1), 0.0), "0.7 kg in the waterskin is not enough to draw");
		g->player->water_carried = 1.0;
		check(water_hint(*g, V3(-3, 0, -1), 0.0), "1 kg in the waterskin draws");
	}
	auto g = make_game("conduction");
	g->player->element = Sim::WATER;
	g->player->water_carried = 0.0;
	check(water_hint(*g, V3(-5.2, 0, 5.5), kPi), "a puddle in the aim cone: hint lit");
	check(!water_hint(*g, V3(-5.2, 0, 5.5), 0.0), "the same puddle behind you: hint dimmed");
}

FF_TEST(test_regressions_game, test_quality_and_perf_log_cases_are_presentation_only) {
	note("test_quality_steps_down_once_when_the_lower_tier_is_fast, test_quality_still_steps_again_when_the_lower_tier_is_slow and "
	     "test_perf_session_log_is_kept_only_when_recording exercise Game._adapt_quality / PerfMonitor (presentation): not part of "
	     "FourfoldCore (see docs/core/PORT_STATUS.md)");
	check(true, "documented skip");
}
