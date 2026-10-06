// Port of game/tests/sim/test_kit_air_vortex.gd: Air / Vortex (sub 1): Twister / Tornado / Cyclone Fortress (a walking
// ZONE that captures, orbits and lifts), infusions and the neutral rule, Spiral Lance, Dust Funnel, Eddy Ring, Vortex
// Wall + Catch, Unleash, Funnel Down, Eye of the Storm (steer, contest), Spin Step, Whirl Lift, the Vortex column.
// The animation clip existence check is not ported (clip table: animation stream).
#include "ff_test.h"
#include "kit_air_util.h"
#include "sim_harness.h"

#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
const char* const MOVES[] = {"vortex_twister", "vortex_spiral", "vortex_funnel", "vortex_eddy", "vortex_wall", "vortex_unleash", "vortex_funnel_down",
                             "vortex_eye", "vortex_spin_step", "vortex_whirl"};

struct AV : AirCase {
	std::pair<ActorState*, ActorState*> _duel(double dist = 10.0) { return duel(1, Sim::EARTH, 3, dist); }
	// A tornado zone for rule-level tests (the move makes the same body): tag tornado at p, owner A.
	BodyRef _tornado(ActorState* owner, Vec3 p, double radius = 2.5, double power = 25.0, int tier = 2) {
		MatBody* z = h().spawn_zone("tornado", p, radius, owner, power);
		z->tier = tier;
		z->sub = 1;
		z->props.set("height", 5.0);
		z->props.set("dps", 2.0);
		return keep(z);
	}
	static double flat(Vec3 a, Vec3 b) { return static_cast<double>(Vec2(a.x - b.x, a.z - b.z).length()); }
	std::string predict(const AgentRef& th, const AgentRef& c) { return Interactions::predict(h().w, *th, *c).outcome; }
};
}  // namespace

FF_TEST(test_kit_air_vortex, test_every_vortex_move_has_a_def_and_a_binding) {
	Moves::ensure_ready();
	for (const char* idc : MOVES) {
		const std::string id = idc;
		check(Moves::defs().has(id), id + " registered");
		if (!Moves::defs().has(id)) continue;
		const Dict d = Moves::defs().get(id).as_dict();
		for (const char* k : {"name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"}) check(d.has(k), id + " has " + k);
		check(dint(d, "sub") == 1 && dint(d, "element") == 3, id + " is Air/Vortex");
		const std::string slot = dstr(d, "slot");
		if (in_list(slot, {"strike", "thrust", "ground", "sweep", "tech"})) {
			check(d.has("tiers") && ddict(d, "tiers").has("t3"), id + " has tiers up to t3");
			check(d.has("counter") && d.has("threat"), id + " has counter + threat");
		}
		check(Moves::slot_of(3, 1, id) == slot, id + " bound to " + slot);
	}
	for (const char* slot : Sim::SLOTS) {
		const std::string id = Moves::resolve(3, 1, slot);
		check(!id.empty() && dint(Moves::defs().get(id).as_dict(), "sub", 0) == 1, std::string("Air/Vortex ") + slot + " bound (" + id + ")");
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_twister_tornado_fortress_tiers) {
	const double want[2][2] = {{1, 8.0}, {2, 12.0}};
	for (int tier = 0; tier < 2; ++tier) {
		auto [a, r] = _duel(14.0);
		(void)r;
		SimHarness& h = this->h();
		run_move(a, "vortex_twister", tier, tier == 0 ? 1 : 30, 13);
		const std::vector<BodyRef> bs = bodies_tagged("twister");
		check(static_cast<double>(bs.size()) == want[tier][0], S("T", tier, ": ", want[tier][0], " twister(s) (", bs.size(), ")"));
		for (const BodyRef& b : bs) {
			check(b->mat == Mat::Air && std::fabs(b->power - want[tier][1]) < 1e-6 && b->attack_id != 0, S("T", tier, ": AIR twister P ", want[tier][1]));
			near(b->vel.length(), 12.0, 0.6, "12 m/s");
		}
		h.step(120);
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
	}
	const double zw[2][3] = {{2.5, 4.0, 25.0}, {4.0, 6.0, 35.0}};
	for (int k = 0; k < 2; ++k) {
		auto [a2, r2] = _duel(14.0);
		(void)r2;
		SimHarness& h = this->h();
		run_move(a2, "vortex_twister", 2 + k, 30, 13);
		const std::vector<BodyRef> zs = zones_tagged("tornado");
		check(zs.size() == 1, S("T", 2 + k, ": one tornado zone"));
		if (zs.size() == 1) {
			near(zs[0]->zone_radius, zw[k][0], 1e-6, S("T", 2 + k, " radius"));
			near(zs[0]->max_life, zw[k][1], 0.3, S("T", 2 + k, " lives ", zw[k][1], " s"));
			near(zs[0]->power, zw[k][2], 1e-6, S("T", 2 + k, " power"));
			check(zs[0]->owner == a2->id && dnum(zs[0]->props, "walk_speed", 0.0) == 4.0, "owned, walks 4 m/s");
		}
		fx_catalogued(S("tornado T", 2 + k));
		h.step(80);
		check(a2->action == nullptr, S("T", 2 + k, " ends cleanly"));
		h.step(420);
		check(zones_tagged("tornado").empty(), "the tornado ends on its own");
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_a_tornado_walks_to_the_target_and_lifts_the_fighter) {
	auto [a, r] = _duel(12.0);
	SimHarness& h = this->h();
	run_move(a, "vortex_twister", 2, 30, 13);
	const std::vector<BodyRef> zs = zones_tagged("tornado");
	if (!check(!zs.empty(), "a tornado")) return;
	const BodyRef z = zs[0];
	const double d0 = flat(z->pos, r->pos);
	h.step(60);
	const double d1 = flat(z->pos, r->pos);
	check(d0 - d1 > 3.0 && d0 - d1 < 5.0, S("walks ~4 m/s toward the rival (", d0, " -> ", d1, " m)"));
	h.step(150);
	double peak = 0.0;
	for (int k = 0; k < 120; ++k) {
		h.step();
		peak = maxf(peak, r->pos.y);
	}
	check(peak > 1.0, S("the rival is lifted (peak ", peak, " m)"));
	check(r->health < 100.0, S("and hurt (", r->health, ")"));
	check(a->health == 100.0, "the caster's own tornado spares them");
}

FF_TEST_F(test_kit_air_vortex, AV, test_a_tornado_captures_a_light_stone_and_slows_a_heavy_one) {
	auto [a, r] = _duel(14.0);
	SimHarness& h = this->h();
	const BodyRef z = _tornado(a, V3(0, 0, a->pos.z - 4.0f));
	const BodyRef light = keep(h.launch_at(a, "stone", 20.0, 10.0, Sim::AMBIENT_C, "", r, 6.0));
	h.step(40);
	check(light->captured_by == z->id, "a 20 kg shot is captured");
	check(h.has_event("capture", "body", Value(light->id)), "capture event");
	check(light->attack_id == 0, "and is harmless inside");
	h.step(60);
	check(light->alive && light->pos.distance_to(z->pos) < z->zone_radius + 1.0, S("it orbits in the zone (", light->pos.distance_to(z->pos), " m)"));
	// heavy: 45 kg only slowed / bent
	const BodyRef heavy = keep(h.launch_at(a, "stone", 45.0, 12.0, Sim::AMBIENT_C, "", r, 6.0));
	h.step(30);
	check(heavy->captured_by != z->id, "a 45 kg stone is not captured");
	check(h.any_event("interaction",
	                  [](const Dict& e) { return ev_s(e, "threat") == "stone_heavy" && ev_s(e, "counter") == "tornado" && ev_s(e, "outcome") == "slow"; }),
	      "it is slowed and bent");
	const BodyRef boulder = keep(h.launch_at(a, "stone", 200.0, 9.0, Sim::AMBIENT_C, "", r, 7.0));
	h.step(30);
	check(boulder->captured_by != z->id && boulder->alive, "a boulder ignores the tornado");
}

FF_TEST_F(test_kit_air_vortex, AV, test_infusions_sand_fire_water_steam) {
	struct Case {
		const char* kind;
		Mat mat;
		const char* status;
	};
	const Case cases[] = {{"sand", Mat::Sand, "blinded"}, {"fire", Mat::Fire, "burning"}, {"water", Mat::Water, "wet"}, {"steam", Mat::Steam, "scalded"}};
	for (const Case& cs : cases) {
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		const BodyRef z = _tornado(a, V3(r->pos.x, 0, r->pos.z + 0.5f));
		const BodyRef b = keep(h.w->spawn_body(cs.mat, Form::Chunk, cs.mat != Mat::Fire ? 2.0 : 0.5, z->pos + V3(0.6, 1.0, 0), "test"));
		if (cs.mat == Mat::Sand) {
			h.w->mass_ledger.ground_taken += 2.0;
		} else if (cs.mat == Mat::Fire) {
			b->heat_payload = 400.0;
			h.w->ledger.generated += 400.0;
		} else if (cs.mat == Mat::Steam) {
			b->mass = 0.5;
			b->update_radius();
			h.w->mass_ledger.vapor += 0.0;
		}
		b->attack_id = 0;
		b->gravity_scale = 0.0;
		h.step(30);
		const std::string k = cs.kind;
		check(b->captured_by == z->id, k + ": captured");
		check(dstr(z->props, "infused", "").find(k) != std::string::npos, k + ": the tornado is infused (" + dstr(z->props, "infused", "") + ")");
		check(Status::has(*r, cs.status), k + ": the rival inside gets " + cs.status);
		if (k == "water") check(Materials::conducts(*z), "a water tornado conducts (lightning node)");
		check(z->owner == a->id, "own material: still A's tornado");
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_a_tornado_infused_by_the_enemys_fire_turns_neutral) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	const BodyRef z = _tornado(a, V3(0, 0, a->pos.z - 3.0f));
	const BodyRef ball = keep(h.w->spawn_body(Mat::Fire, Form::Chunk, 0.5, z->pos + V3(0.5, 1.0, 0), "test"));
	ball->heat_payload = 400.0;
	h.w->ledger.generated += 400.0;
	ball->attack_id = h.w->new_attack_id();
	ball->attack_owner = r->id;
	ball->hit_set.add(r->id);
	ball->vel = V3(0, 0, 2.0);
	ball->gravity_scale = 0.0;
	h.step(30);
	check(ball->captured_by == z->id, "the rival's fireball is caught");
	check(z->owner == -1 && dbool(z->props, "neutral", false), "infused by the enemy: neutral (owner -1)");
	check(h.has_event("infuse", "neutral", Value(true)), "infuse event (neutral)");
	// neutral: it hurts both - the former owner standing inside is no longer spared
	a->pos = V3(z->pos.x + 0.5f, 0, z->pos.z);
	const double hp0 = a->health;
	h.step(60);
	check(a->health < hp0, S("the neutral whirl hurts its own caster (", hp0, " -> ", a->health, ")"));
}

FF_TEST_F(test_kit_air_vortex, AV, test_cyclone_fortress_sets_a_lava_wave_and_a_tornado_only_crusts_it) {
	// 20 kg wave (27.3): Tornado T2 (25) 0.92 partial: crust + spatter (magma vortex); Fortress T3 (35) 1.28: sets it into rock.
	struct Cs {
		double power, radius;
		bool partial;
	};
	for (const Cs& cs : {Cs{25.0, 2.5, true}, Cs{35.0, 4.0, false}}) {
		auto [a, r] = _duel(14.0);
		(void)r;
		SimHarness& h = this->h();
		const BodyRef z = _tornado(a, V3(0, 0, a->pos.z - 6.0f), cs.radius, cs.power, cs.partial ? 2 : 3);
		const BodyRef wave = lava_wave(20.0, V3(0, 0, a->pos.z - 9.0f));
		const Snap base = snap(*h.w);
		const double liquid0 = wave->liquid;
		h.step(14);
		const std::string label = cs.partial ? "partial" : "full";
		check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "threat") == "lava_wave" && ev_s(e, "counter") == "tornado"; }),
		      label + ": the wave met the tornado through the rules");
		if (cs.partial) {
			check(wave->alive && wave->form == Form::Wave && wave->liquid < liquid0 - 0.04 && wave->liquid > 0.5,
			      S("Tornado T2: crusts it (liquid ", wave->liquid, "), it is not set"));
			check(wave->wave_budget < 6.0, S("its reach collapses (budget ", wave->wave_budget, " of 12 m): the partial removed CP_eff from its power"));
			check(dstr(z->props, "infused", "").find("magma") != std::string::npos || dnum(z->props, "spatter_until", -1.0) > 0.0,
			      "and picks up spatter: a magma vortex");
		} else {
			check(wave->form != Form::Wave && wave->liquid <= 0.0, S("Cyclone Fortress sets the wave into rock (", Sim::form_name(wave->form), ")"));
		}
		ledgers_ok(base, "lava vs tornado", 1e-4);
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_vortex_wall_captures_a_volley_and_unleash_returns_it_as_your_attack) {
	auto [a, r] = _duel(12.0);
	SimHarness& h = this->h();
	h.press(a, "guard");
	h.step(20);
	const std::vector<BodyRef> walls = zones_tagged("vortex_wall");
	check(walls.size() == 1 && walls[0]->owner == a->id, "the Vortex Wall is a zone around the guard");
	if (walls.empty()) return;
	const Snap base = snap(*h.w);
	std::vector<BodyRef> stones;
	for (int k = 0; k < 2; ++k) stones.push_back(keep(h.launch_at(a, "stone", 20.0, 14.0, Sim::AMBIENT_C, "", r, 6.0 + 1.5 * k)));
	h.step(45);
	check(stones[0]->captured_by == walls[0]->id && stones[1]->captured_by == walls[0]->id, "both shots are captured, not stopped");
	check(a->health == 100.0, "nothing reached A");
	const int ids[2] = {stones[0]->attack_id, stones[1]->attack_id};
	h.log.clear();
	h.flick(a, "guard", static_cast<int>(Gesture::Up));
	h.step(3);
	check(a->action != nullptr && a->action->id == "vortex_unleash", "guard flick up = Unleash");
	h.release(a, "guard");
	h.step(30);
	const std::vector<Dict> un = h.events("unleash");
	check(un.size() == 2, S("both bodies unleashed (", un.size(), ")"));
	for (const BodyRef& b : stones) {
		check(b->attack_owner == a->id && b->attack_id != 0, S("#", b->id, " flies as A's attack"));
		check(b->attack_id != ids[0] && b->attack_id != ids[1], "with a NEW attack id");
		check(b->vel.dot(r->pos - b->pos) > 0.0f || b->attack_id == 0, "toward the rival");
	}
	check(zones_tagged("vortex_wall").empty(), "the wall is spent");
	h.step(80);
	ledgers_ok(base, "unleash", 1e-5);
	check(r->health < 100.0, S("the returned volley hurts R (", r->health, ")"));
}

FF_TEST_F(test_kit_air_vortex, AV, test_vortex_catch_takes_a_heavier_shot_only_when_perfect) {
	// a 40 kg stone: Vortex Wall alone only slows it; a perfect Wall (Vortex Catch) catches shots up to 45 kg
	for (bool perfect : {false, true}) {
		auto [a, r] = _duel(12.0);
		SimHarness& h = this->h();
		if (!perfect) {
			h.press(a, "guard");
			h.step(40);
		}
		BodyRef st;
		if (perfect) {
			// spawn first, press the guard 4 ticks before it reaches the wall's edge
			st = keep(h.launch_at(a, "stone", 40.0, 12.0, Sim::AMBIENT_C, "", r, 4.8));
			h.step(4);
			h.press(a, "guard");
		} else {
			st = keep(h.launch_at(a, "stone", 40.0, 12.0, Sim::AMBIENT_C, "", r, 6.0));
		}
		h.step(40);
		const std::vector<BodyRef> walls = zones_tagged("vortex_wall");
		const bool caught = !walls.empty() && st->captured_by == walls[0]->id;
		if (perfect) {
			check(caught, "Vortex Catch (perfect) catches a 40 kg stone");
			check(h.has_event("vortex_catch"), "vortex_catch event");
		} else {
			check(!caught, "a plain Vortex Wall does not catch a 40 kg stone");
		}
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_funnel_down_drops_the_captured_at_your_feet) {
	auto [a, r] = _duel(12.0);
	SimHarness& h = this->h();
	h.press(a, "guard");
	h.step(20);
	const BodyRef st = keep(h.launch_at(a, "stone", 20.0, 14.0, Sim::AMBIENT_C, "", r, 6.0));
	h.step(40);
	check(st->captured_by >= 0, "captured");
	h.flick(a, "guard", static_cast<int>(Gesture::Down));
	h.step(3);
	check(a->action != nullptr && a->action->id == "vortex_funnel_down", "guard flick down = Funnel Down");
	h.step(25);
	check(st->captured_by < 0 && st->attack_id == 0 && st->pos.distance_to(a->pos) < 2.5f,
	      S("dropped at the caster's feet, harmless (", st->pos.distance_to(a->pos), " m)"));
	check(zones_tagged("vortex_wall").empty(), "the wall is gone");
}

FF_TEST_F(test_kit_air_vortex, AV, test_dust_funnel_turns_rubble_into_ammunition) {
	auto [a, r] = _duel(14.0);
	(void)r;
	SimHarness& h = this->h();
	std::vector<BodyRef> rub;
	for (int k = 0; k < 2; ++k) {
		MatBody* b = h.w->spawn_body(Mat::Stone, Form::Chunk, 6.0, V3(0.4 * k - 0.2, 0.2, a->pos.z - 4.0f - 2.0f * static_cast<float>(k)), "test");
		h.w->mass_ledger.ground_taken += 6.0;
		b->on_ground = true;
		rub.push_back(keep(b));
	}
	const Snap base = snap(*h.w);
	run_move(a, "vortex_funnel", 1, 1, 14);
	const std::vector<BodyRef> f = bodies_tagged("funnel");
	check(f.size() == 1 && f[0]->form == Form::Wave && f[0]->mat == Mat::Air, "the funnel is an AIR wave (tag funnel)");
	h.step(40);
	check(rub[0]->captured_by >= 0 || rub[1]->captured_by >= 0, "it gathers the loose rubble on its path");
	h.step(140);
	int flung = 0;
	for (const BodyRef& b : rub)
		if (h.has_event("release_captured", "body", Value(b->id))) ++flung;
	check(flung >= 1, S("the carried rubble is released at the end (", flung, ")"));
	ledgers_ok(base, "funnel", 1e-5);
}

FF_TEST_F(test_kit_air_vortex, AV, test_spiral_lance_tiers_and_class) {
	const int pierce[4] = {0, 0, 1, 2};
	const double power[4] = {10.0, 14.0, 18.0, 24.0};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _duel(14.0);
		(void)r;
		SimHarness& h = this->h();
		run_move(a, "vortex_spiral", tier, tier == 0 ? 1 : 30, 13);
		const std::vector<BodyRef> bs = bodies_tagged("spiral");
		check(bs.size() == 1 && std::fabs(bs[0]->power - power[tier]) < 1e-6, S("T", tier, ": a spiral P ", power[tier]));
		if (bs.size() == 1) {
			check(dint(bs[0]->props, "pierce", 0) == pierce[tier], S("T", tier, " pierce ", pierce[tier]));
			check(Interactions::classify(*bs[0]) == "spiral", "its own threat class");
			near(bs[0]->vel.length(), tier == 3 ? 28.0 : 24.0, 0.8, "speed");
		}
		h.step(90);
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
	}
}

FF_TEST_F(test_kit_air_vortex, AV, test_eddy_ring_curves_projectiles_and_pushes_fighters_out) {
	{
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		run_when(a, "vortex_eddy", 2, [&]() { h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 7.0); }, 28, 0);
		h.until([&]() { return h.has_event("interaction"); }, 80);
		const std::vector<Dict> ix = h.filter("interaction", [](const Dict& e) { return ev_s(e, "counter") == "eddy"; });
		check(!ix.empty() && in_list(ev_s(ix[0], "outcome"), {"bend", "deflect", "reflect"}),
		      "the stone is curved around the ring: " + (!ix.empty() ? ev_s(ix[0], "outcome") : std::string("-")));
		check(a->health == 100.0, "A is untouched");
	}
	// a rival inside the ring is pushed out
	auto [a2, r2] = _duel(10.0);
	r2->pos = a2->pos + V3(0, 0, -1.2);
	const double d0 = r2->pos.distance_to(a2->pos);
	run_move(a2, "vortex_eddy", 0, 1, 30);
	check(r2->pos.distance_to(a2->pos) > d0 + 0.3, S("a light rival is pushed out (", d0, " -> ", r2->pos.distance_to(a2->pos), " m)"));
}

FF_TEST_F(test_kit_air_vortex, AV, test_eye_of_the_storm_summons_steers_and_lingers) {
	auto [a, r] = _duel(14.0);
	(void)r;
	SimHarness& h = this->h();
	h.press(a, "tech");
	h.step(40);
	const std::vector<BodyRef> zs = zones_tagged("tornado");
	check(zs.size() == 1 && zs[0]->owner == a->id, "the Eye makes a tornado at the aim point");
	if (zs.size() == 1) {
		const BodyRef z = zs[0];
		const double dist = flat(z->pos, a->pos);
		check(dist > 7.0 && dist <= 10.5, S("~10 m away (", dist, ")"));
		check(z->max_life < 0.0, "kept alive while held");
		// steer: aim to the side
		const double x0 = z->pos.x;
		h.aim(a, V3(1, 0, -0.2));
		h.step(60);
		check(z->pos.x > x0 + 0.5, S("steered by the aim (", x0, " -> ", z->pos.x, ")"));
		h.release(a, "tech");
		h.step(5);
		check(z->max_life > 0.0 && z->max_life - z->age < 2.2, "released: lives about 2 s more");
		h.step(200);
		check(!z->alive, "and then it is gone");
	}
	check(a->action == nullptr, "ends cleanly");
}

FF_TEST_F(test_kit_air_vortex, AV, test_eye_of_the_storm_takes_over_an_enemy_tornado) {
	auto [a, r] = _duel(14.0);
	SimHarness& h = this->h();
	const BodyRef z = keep(h.spawn_zone("tornado", V3(0, 0, a->pos.z - 9.0f), 2.5, r, 25.0));
	z->props.set("height", 5.0);
	z->tier = 2;
	h.aim(a, V3(0, 0, -1));
	h.press(a, "tech");
	h.step(40);
	check(z->owner == a->id, "contested: the Eye (25) takes the enemy tornado (25) over");
	check(h.has_event("tornado_taken"), "event tornado_taken");
	h.release(a, "tech");
	h.step(30);
}

FF_TEST_F(test_kit_air_vortex, AV, test_two_tornadoes_meet_and_the_stronger_absorbs_the_weaker) {
	auto [a, r] = _duel(14.0);
	SimHarness& h = this->h();
	const BodyRef big = _tornado(a, V3(0, 0, 0), 4.0, 35.0, 3);
	const BodyRef small = keep(h.spawn_zone("tornado", V3(1.0, 0, 0), 2.5, r, 25.0));
	small->props.set("height", 5.0);
	h.step(20);
	check(!small->alive && big->alive, "the Fortress absorbs the smaller tornado");
	check(big->power > 35.0, S("and keeps part of its power (", big->power, ")"));
	check(h.has_event("tornado_contest"), "contest event");
}

FF_TEST_F(test_kit_air_vortex, AV, test_spin_step_deflects_a_light_shot_and_whirl_lift_hovers) {
	{
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		h.launch_at(a, "stone", 20.0, 14.0, Sim::AMBIENT_C, "", r, 4.5);
		h.it(a).move = V3(1, 0, 0);
		h.press(a, "evade");
		h.step(30);
		check(a->health == 100.0, "Spin Step: A is not hit (i-frames + spin)");
		check(a->action == nullptr || a->action->id == "vortex_spin_step", "the evade is the Spin Step");
		check(h.has_event("evade", "move", Value("vortex_spin_step")), "evade event");
	}
	// Whirl Lift
	auto [a2, r2] = _duel(10.0);
	(void)r2;
	SimHarness& h = this->h();
	h.press(a2, "evade");
	h.it(a2).evade_held = true;
	h.step(16);
	check(a2->action != nullptr && a2->action->id == "vortex_whirl", "held 0.2 s: Whirl Lift");
	h.step(60);
	check(a2->pos.y > 1.2f && a2->pos.y < 1.8f, S("hovers about 1.5 m (", a2->pos.y, ")"));
	check(Status::immune(*a2, "ground"), "ground lines pass under");
	check(!zones_tagged("eddy").empty(), "inside a small vortex");
	h.it(a2).evade_held = false;
	h.step(40);
	check(zones_tagged("eddy").empty(), "the vortex ends with the hold");
}

FF_TEST_F(test_kit_air_vortex, AV, test_vortex_column_cells_at_reference_powers) {
	auto [a, r] = _duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	const std::pair<const char*, double> rows[] = {{"stone", 17.0}, {"metal", 12.0}, {"sand", 10.0}, {"water", 9.6}, {"ice", 9.0}};
	for (const auto& row : rows) {
		const std::string t = row.first;
		AgentRef th = threat(*h.w, t, row.second, t != "stone" ? 10.0 : 20.0);
		AgentRef ctr = Agent::of_move(h.w, a, "vortex_twister", 2, false);
		check(predict(th, ctr) == "air_infuse", t + " x Tornado T2: caught");
	}
	AgentRef vw = Agent::of_move(h.w, a, "vortex_wall", 0, false);
	check(vw->ccls == "wall_vortex" && std::fabs(vw->power - 16.0) < 1e-6, "Vortex Wall CP 16");
	check(predict(threat(*h.w, "stone", 17.0, 20.0), vw) == "air_infuse", "stone (17) x Vortex Wall 16: caught (partial)");
	check(predict(threat(*h.w, "stone", 80.0, 20.0), vw) == "bend", "a very hard shot only bends");
	check(predict(threat(*h.w, "water_wave", 45.0, 45.0), vw) == "overwhelm", "water mass drowns the Vortex Wall");
	const std::string heavy = predict(threat(*h.w, "stone_heavy", 31.5, 45.0), Agent::of_move(h.w, a, "vortex_twister", 2, false));
	check(heavy == "air_slow_bend", "a 45 kg stone: bend + slow (" + heavy + ")");
	AgentRef tor = Agent::of_move(h.w, a, "vortex_twister", 2, false);
	check(predict(threat(*h.w, "flame", 20.0, 0.0, "H"), tor) == "air_infuse", "a strong flame feeds the tornado (AMP)");
	check(predict(threat(*h.w, "flame", 8.0, 0.0, "H"), tor) == "extinguish", "a small flame (>= 2x) is put out");
	check(predict(threat(*h.w, "lightning", 24.0, 0.0, "E"), tor) == "pass", "lightning passes");
	check(predict(threat(*h.w, "gust", 11.0, 0.0, "P"), tor) == "absorb", "a gust is absorbed (spins faster)");
	check(predict(threat(*h.w, "sound", 16.0, 0.0, "P"), tor) == "weaken", "sound weakens it (x0.6)");
}

FF_TEST_F(test_kit_air_vortex, AV, test_vortex_kit_is_deterministic_and_ledgers_balance) {
	std::vector<std::string> hashes;
	for (int k = 0; k < 2; ++k) {
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		h.launch_at(a, "stone", 20.0, 12.0, Sim::AMBIENT_C, "", r, 6.0);
		run_move(a, "vortex_twister", 2, 30, 13);
		h.step(200);
		run_move(a, "vortex_eddy", 1, 28, 40);
		ledgers_ok(base, "vortex exchange", 1e-5);
		finite_world("vortex");
		hashes.push_back(S(a->pos.x, ",", a->pos.y, ",", a->pos.z, "|", r->pos.x, ",", r->pos.z, "|", h.w->bodies.size(), "|", ftos(a->focus, 4), "|",
		                   ftos(r->health, 4)));
	}
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: " + hashes[0] + " vs " + hashes[1]);
}
