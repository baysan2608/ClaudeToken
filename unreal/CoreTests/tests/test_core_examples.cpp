// Port of game/tests/sim/test_core_examples.gd: the owner's examples of docs/MOVESET.md §5.4 at rule / verb level, with
// test-local defs and rules (the element kits ship the real moves): lava vs gust T0-T3, lightning through stone, sink a
// stone, a wave carries a stone back, wind guard deflect / perfect return, melt a wall (slump), split it and spike it back.
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Conduction.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
struct Ex : HarnessCase {
	double _energy_drift(double e0) { return absf(h().w->system_energy() - h().w->ledger_balance() - e0); }
	BodyRef _lava_wave(double mass) {
		SimHarness& hh = h();
		MatBody* b = hh.w->spawn_body(Mat::Stone, Form::Wave, mass, V3(0, 0, 0), "test");
		hh.w->mass_ledger.ground_taken += mass;
		hh.w->ledger.generated += Thermal::heat(*b, mass * (Sim::STONE_C * 980.0 + Sim::STONE_LATENT));
		Thermal::update_phase(*b);
		b->wave_dir = V3(0, 0, 1);
		b->wave_budget = 12.0;
		b->vel = V3(0, 0, 7.5);
		b->attack_id = hh.w->new_attack_id();
		return b->shared_from_this();
	}
	struct BoltWorld {
		ActorState* c;
		ActorState* t;
		BodyRef wall;
	};
	BoltWorld _bolt_world() {
		SimHarness& hh = H(1);
		ActorState* c = hh.actor("C", V3(0, 0, 6), 0, D({{"lightning", true}}), Sim::FIRE);
		ActorState* t = hh.actor("T", V3(0, 0, -3), 1, Dict(), Sim::EARTH);
		MatBody* wall = hh.w->spawn_body(Mat::Stone, Form::Wall, 120.0, V3(0, 0, 0), "test");
		hh.w->mass_ledger.ground_taken += 120.0;
		wall->wall_half = V3(1.1, 0.75, 0.28);
		wall->wall_rise = 1.0;
		wall->static_body = true;
		wall->props.set("standing", 999.0);
		hh.step(5);
		hh.log.clear();
		return {c, t, wall->shared_from_this()};
	}
	static Dict bolt(double damage, double range = 14.0) {
		return D({{"range", range}, {"damage", damage}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	}
};
}  // namespace

FF_TEST_F(test_core_examples, Ex, test_lava_vs_gust_needs_a_t3_gale) {
	SimHarness& h = H(1);
	h.begin_scope();
	// Air kit cell (test-local): strong wind sets lava (convective cooling, booked as ambient).
	Interactions::add_rule("molten", "gust", D({{"tiers", A({2, 3})}, {"outcome", "transform"}, {"to", "rock"}, {"partial", "weaken"}, {"fail", "pass"}}));
	const double e0 = h.w->system_energy() - h.w->ledger_balance();
	const double powers[4] = {7.0, 11.0, 18.0, 28.0};
	std::vector<std::string> outs;
	std::vector<std::pair<double, double>> tps;
	for (int tier = 0; tier < 4; ++tier) {
		const BodyRef lava = _lava_wave(20.0);
		const double tp0 = Interactions::threat_power(*Agent::of_body(*h.w, *lava), Dict());
		AgentRef g = Agent::of_volume(h.w, nullptr, nullptr, "gust", V3(0, 1, -3), V3(0, 0, 1), D({{"P", powers[tier]}}));
		g->tier = tier;
		AgentRef th = Agent::of_body(*h.w, *lava);
		const IxResult r = Interactions::resolve(*h.w, *th, *g);
		outs.push_back(r.outcome);
		tps.push_back({tp0, Interactions::threat_power(*Agent::of_body(*h.w, *lava), Dict())});
		if (tier == 3) {
			h.step();
			check(lava->form != Form::Wave && lava->liquid <= 0.0, "T3: the front stalls and sets into rock");
		}
		h.w->decay_body(*lava, "test");
	}
	near(tps[0].first, 27.3, 0.01, "20 kg lava wave TP 27.3");
	check(outs[0] == "pass" && outs[1] == "pass", "Palm Gust / Cyclone (7, 11) can't stop lava: " + outs[0] + " " + outs[1]);
	check(outs[2] == "weaken", "Gale T2 (18, ratio 0.66) crusts and slows: " + outs[2]);
	near(tps[2].second, 27.3 - 18.0, 0.05, "the partial removes CP_eff from TP");
	check(outs[3] == "transform", "Hurricane Palm T3 (28, ratio 1.03) sets it: " + outs[3]);
	check(_energy_drift(e0) < 1e-6, "every removed HU is booked (ambient)");
	h.end_scope();
}

FF_TEST_F(test_core_examples, Ex, test_lightning_grounds_in_stone_and_a_storm_bolt_blasts_through) {
	BoltWorld s = _bolt_world();
	SimHarness& h = this->h();
	int aid = h.w->new_attack_id();
	const Dict out = Conduction::discharge(*h.w, *s.c, s.t->chest(), bolt(24.0), aid, true);
	h.step();
	check(dbool(out, "blocked") && s.wall->alive && s.t->health == 100.0, "Bolt E 24 vs Bulwark 30: grounded, the wall stands");
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "counter") == "wall_stone" && ev_s(e, "outcome") == "ground"; }), "interaction ground");
	aid = h.w->new_attack_id();
	const Dict out2 = Conduction::discharge(*h.w, *s.c, s.t->chest(), bolt(36.0, 16.0), aid, true);
	h.step();
	check(!dbool(out2, "blocked") && !s.wall->alive && h.has_event("wall_crumble"), "Storm Bolt E 36 shatters the wall");
	near(dnum(out2, "e"), 21.0, 1e-6, "it continues with 36 - 0.5 x 30 = 21");
	near(100.0 - s.t->health, 21.0, 1e-6, "and hits the fighter behind it for 21");
}

FF_TEST_F(test_core_examples, Ex, test_ice_insulates_bolts_up_to_1_5_cp) {
	BoltWorld s = _bolt_world();
	SimHarness& h = this->h();
	h.w->decay_body(*s.wall, "test");
	const BodyRef ice = h.w->spawn_body(Mat::Water, Form::Wall, 50.0, V3(0, 0, 0), "test", -5.0)->shared_from_this();
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	ice->tag = "ice";
	ice->wall_half = V3(1.1, 0.75, 0.28);
	ice->wall_rise = 1.0;
	ice->static_body = true;
	ice->props.set("standing", 999.0);
	int aid = h.w->new_attack_id();
	const Dict out = Conduction::discharge(*h.w, *s.c, s.t->chest(), bolt(24.0), aid, true);
	check(dbool(out, "blocked") && ice->alive, "ice wall 50 kg (CP 22 x 1.5 = 33) stops E 24");
	aid = h.w->new_attack_id();
	const Dict out2 = Conduction::discharge(*h.w, *s.c, s.t->chest(), bolt(36.0), aid, true);
	check(!dbool(out2, "blocked") && !ice->alive, "E 36 > 33 shatters it");
}

FF_TEST_F(test_core_examples, Ex, test_sink_a_stone_into_the_ground) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::register_def("t_swallow", D({{"verb", "zone"}, {"counter", D({{"cls", "swallow"}, {"power", A({22, 30, 40, 55})}})}}));
	Interactions::add_rule("solid_light", "swallow", D({{"outcome", "sink"}, {"partial", "weaken"}}));
	ActorState* e = h.actor("E", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	const BodyRef stone = h.launch_at(e, "stone", 20.0, 17.0)->shared_from_this();
	const double m0 = h.w->stone_mass();
	AgentRef t = Agent::of_body(*h.w, *stone, e);
	const IxResult pr = h.predict(*t, "t_swallow", 0, false, e);
	check(pr.outcome == "sink" && is_equal_approx(pr.ratio, 22.0 / 17.0), "Swallow CP 22 >= 17: sink");
	const IxResult r = Interactions::resolve(*h.w, *t, *Agent::of_move(h.w, e, "t_swallow", 0, false));
	check(r.stopped && !stone->alive, "the stone is gone into the ground");
	near(h.w->mass_ledger.ground_returned, 20.0, 1e-9, "booked as ground_returned");
	near(h.w->stone_mass(), m0, 1e-9, "stone mass conserved");
	h.end_scope();
}

FF_TEST_F(test_core_examples, Ex, test_a_wave_carries_the_stone_back) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::register_def("t_tide", D({{"element", 1}, {"verb", "ground_line"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.3}, {"cost", 8.0},
	                                  {"source", "waterskin"}, {"mat", "water"}, {"mass", 6.0}, {"tag", "water_wave"}, {"speed", 9.0}, {"budget", 10.0},
	                                  {"width", 2.0}, {"damage", 8.0}, {"balance", 30.0}, {"power", 18.0}, {"channel", "K"}}));
	Moves::bind(1, 1, "ground", "t_tide");
	Interactions::add_rule("stone", "wave_water", D({{"outcome", "capture"}, {"partial", "slow"}, {"release_speed", 9.0}}));
	ActorState* wr = h.actor("W", V3(0, 0, 4), 0, Dict(), Sim::WATER);
	ActorState* th = h.actor("T", V3(0, 0, -10), 1, Dict(), Sim::EARTH);
	th->is_dummy = true;
	h.sub(wr, 1);
	h.step(20);
	const double w0 = h.w->water_mass();
	const BodyRef stone = h.launch_at(wr, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", th)->shared_from_this();
	h.flick(wr, "attack", static_cast<int>(Gesture::Down));
	h.step();
	h.release(wr, "attack");
	h.until([&]() { return h.has_event("capture"); }, 40);
	check(h.has_event("capture", "body", Value(stone->id)), "the wave captured the stone");
	check(wr->health == 100.0, "it never reached W");
	h.until([&]() { return h.has_event("release_captured"); }, 120);
	check(stone->attack_owner == wr->id && stone->attack_id != 0, "released as W's attack");
	check(stone->vel.dot(th->pos - stone->pos) > 0.0f, "flying back toward the thrower");
	near(h.w->water_mass(), w0, 1e-6, "water mass conserved (wave -> puddle)");
	h.end_scope();
}

FF_TEST_F(test_core_examples, Ex, test_wind_guard_deflects_and_a_perfect_one_returns_the_stone) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::AIR);
	ActorState* t = h.actor("T", V3(0, 0, -8), 1, Dict(), Sim::EARTH);
	t->is_dummy = true;
	h.step(20);
	h.press(a, "guard");
	h.step(30);
	const BodyRef s1 = h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t)->shared_from_this();
	h.until([&]() { return h.has_event("deflect") || h.has_event("hit") || h.has_event("block"); }, 40);
	check(h.has_event("deflect", "actor", Value(a->id)) && a->health == 100.0, "Wind Guard 12 x 1.5 = 18 >= 17: deflected");
	check(s1->attack_id == 0, "the stone is spent");
	h.release(a, "guard");
	h.step(40);
	h.log.clear();
	h.press(a, "guard");
	h.step(2);
	const BodyRef s2 = h.launch_at(a, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", t, 2.5)->shared_from_this();
	h.until([&]() { return h.has_event("perfect_deflect") || h.has_event("hit"); }, 30);
	check(h.any_event("perfect_deflect", [](const Dict& e) { return dstr(e, "verb", "") == "reflect"; }), "perfect (x1.5 = 27): back to the sender");
	check(s2->attack_owner == a->id && s2->vel.dot(t->pos - s2->pos) > 0.0f, "now A's stone, flying at T");
}

FF_TEST_F(test_core_examples, Ex, test_melt_the_wall_and_it_slumps) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::register_def("t_smelter", D({{"element", 2}, {"verb", "ranged_heat"}, {"startup", 0.12}, {"active", 0.0}, {"recovery", 0.2}, {"cost", 6.0},
	                                     {"rate", 450.0}, {"range", 7.0}, {"slump_fraction", 0.25}, {"slump_at", 0.5}}));
	Moves::bind(2, 1, "tech", "t_smelter");
	ActorState* b = h.actor("B", V3(0, 0, -4), 0, Dict(), Sim::EARTH);
	ActorState* f = h.actor("F", V3(0, 0, 2.5), 1, Dict(), Sim::FIRE);
	h.sub(f, 1);
	h.step(20);
	h.press(b, "guard");
	h.step(10);
	MatBody* wallp = h.w->get_body(b->wall_body);
	check(wallp != nullptr && wallp->alive, "setup: Bulwark up");
	if (wallp == nullptr) return;
	const BodyRef wall = wallp->shared_from_this();
	const double m0 = h.w->stone_mass();
	const double e0 = h.w->system_energy() - h.w->ledger_balance();
	h.press(f, "tech");
	const int n = h.until([&]() { return h.has_event("slump"); }, 150);
	check(n > 0, "the wall face slumps");
	note(S("slump after ", static_cast<double>(n) * Sim::DT, " s"));
	check(n > 0 && static_cast<double>(n) * Sim::DT < 1.6, "within about 1 s of Smelter heat");
	const Dict ev = h.last_event("slump");
	MatBody* face = h.w->get_body(ev_i(ev, "body", -1));
	check(face != nullptr && face->liquid >= 0.5 && face->form == Form::Blob, "a molten body");
	check(face != nullptr && is_equal_approx(face->mass, 30.0), "25 % of the 120 kg wall");
	check(face != nullptr && face->pos.z > -2.75f, "on the heated side");
	check(!wall->alive && h.has_event("wall_crumble"), "the rest crumbled");
	h.release(f, "tech");
	h.release(b, "guard");
	h.step(5);
	near(h.w->stone_mass(), m0, 1e-6, "stone mass conserved");
	check(_energy_drift(e0) < 1e-6, S("energy ledger balanced (", _energy_drift(e0), ")"));
	h.end_scope();
}

FF_TEST_F(test_core_examples, Ex, test_split_the_stone_and_spike_it_back) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::register_def("t_seize", D({{"element", 0}, {"verb", "grip"}, {"startup", 0.1}, {"active", 0.06}, {"recovery", 0.28}, {"cost", 6.0},
	                                   {"ccls", "grip_stone"}, {"reach", 7.5}, {"shape", "split"}, {"pieces", 3}, {"spread", 30.0}, {"speed", 20.0},
	                                   {"shape_cost", 3.0}}));
	Moves::bind(0, 1, "tech", "t_seize");
	ActorState* e = h.actor("E", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, -8), 1, Dict(), Sim::FIRE);
	t->is_dummy = true;
	h.sub(e, 1);
	h.step(20);
	const BodyRef stone = h.launch_at(e, "stone", 20.0, 9.0, Sim::AMBIENT_C, "", t, 5.0)->shared_from_this();
	h.press(e, "tech");
	h.until([&]() { return h.w->held(*e) != nullptr; }, 30);
	check(h.w->held(*e) == stone.get(), "seized the incoming stone (REC)");
	h.step(5);
	h.it(e).attack_pressed = true;
	h.step();
	check(h.has_event("shape", "shape", Value("split")), "T+A: split");
	h.release(e, "tech");
	h.step(2);
	const std::vector<Dict> spikes = h.filter("launch", [&](const Dict& x) { return ev_i(x, "actor", -1) == e->id; });
	check(spikes.size() == 3, S("three spikes (", spikes.size(), ")"));
	for (const Dict& x : spikes) {
		MatBody* sb = h.w->get_body(ev_i(x, "body", -1));
		check(sb != nullptr && is_equal_approx(sb->mass, 20.0 / 3.0) && sb->vel.dot(t->pos - sb->pos) > 0.0f, "a third of the stone, flying back");
	}
	h.end_scope();
}
