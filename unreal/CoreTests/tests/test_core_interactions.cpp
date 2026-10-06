// Port of game/tests/sim/test_core_interactions.gd: the counter rule (MOVESET §5; COMBAT_SPEC E4) with synthetic agents.
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct IxFx : HarnessCase {
	AgentRef _counter(const std::string& ccls, double power, int tier = 0, bool perfect = false) {
		AgentRef c = std::make_shared<Agent>();
		c->kind = "move";
		c->ccls = ccls;
		c->cls = c->ccls;
		c->power = power;
		c->tier = tier;
		c->perfect = perfect;
		c->dir = Vec3(1, 0, 0);
		return c;
	}
	MatBody* _stone(double mass, double speed, double temp = Sim::AMBIENT_C) {
		SimHarness& hh = h();
		MatBody* b = hh.w->spawn_body(Mat::Stone, Form::Chunk, mass, Vec3(0, 1.5f, 0), "test", temp);
		hh.w->mass_ledger.ground_taken += mass;
		b->vel = V3(0, 0, speed);
		b->gravity_scale = 0.0;
		b->attack_id = hh.w->new_attack_id();
		return b;
	}
};
}  // namespace

FF_TEST_F(test_core_interactions, IxFx, test_threat_power_channels) {
	SimHarness& h = H(1);
	AgentRef t = Agent::of_body(*h.w, *_stone(20.0, 17.0));
	near(t->ch.K, 17.0, 1e-9, "K = m v / 20 (stone shot 17)");
	near(Interactions::threat_power(*t, Dict()), 17.0, 1e-9, "TP");
	MatBody* lava = h.w->spawn_body(Mat::Stone, Form::Wave, 20.0, Vec3(3, 0, 0), "test");
	h.w->mass_ledger.ground_taken += 20.0;
	Thermal::heat(*lava, 396.0);
	lava->vel = Vec3(7.5f, 0, 0);
	AgentRef la = Agent::of_body(*h.w, *lava);
	check(la->cls == "lava_wave", "class lava_wave (" + la->cls + ")");
	near(Interactions::threat_power(*la, Dict()), 27.3, 0.01, "20 kg lava wave = K 7.5 + H 19.8");
	near(Interactions::threat_power(*la, D({{"w", D({{"H", 2.0}})}})), 7.5 + 39.6, 0.01, "rule weights");
	MatBody* ice = h.w->spawn_body(Mat::Water, Form::Shard, 4.0, Vec3(0, 1, 3), "test", -5.0);
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	AgentRef ia = Agent::of_body(*h.w, *ice);
	near(ia->ch.C, 18.2 / 20.0, 0.01, "4 kg ice at -5 C: C 0.9");
	near(Interactions::threat_power(*ia, Dict(), "heat"), 0.91, 0.01, "cold counts vs heat counters");
	near(Interactions::threat_power(*ia, Dict(), "pressure"), 0.0, 0.01, "cold does not count vs others");
}

FF_TEST_F(test_core_interactions, IxFx, test_bands_full_partial_fail_and_perfect) {
	SimHarness& h = H(1);
	h.begin_scope();
	Interactions::add_rule("stone", "t_counter", D({{"outcome", "deflect"}, {"partial", "weaken"}, {"fail", "overwhelm"}, {"perfect", "reflect"}}));
	AgentRef t = Agent::of_body(*h.w, *_stone(20.0, 17.0));
	IxResult r = Interactions::predict(h.w, *t, *_counter("t_counter", 18.0));
	check(r.band == "full" && r.outcome == "deflect", "18 vs 17: full (" + r.band + " " + r.outcome + ")");
	r = Interactions::predict(h.w, *t, *_counter("t_counter", 12.0));
	check(r.band == "partial" && r.outcome == "weaken", "12 vs 17: partial");
	r = Interactions::predict(h.w, *t, *_counter("t_counter", 8.0));
	check(r.band == "fail" && r.outcome == "overwhelm", "8 vs 17: fail");
	r = Interactions::predict(h.w, *t, *_counter("t_counter", 12.0, 0, true));
	near(r.cp_eff, 18.0, 1e-9, "perfect x1.5");
	check(r.outcome == "reflect", "perfect outcome at full");
	Interactions::add_rule("stone", "t_eff", D({{"eff", 2.0}, {"outcome", "deflect"}, {"partial", "bend"}}));
	r = Interactions::predict(h.w, *t, *_counter("t_eff", 7.0));
	check(r.outcome == "bend" && is_equal_approx(r.ratio, 14.0 / 17.0), S("Palm Gust 7 x 2 = 14 vs 17: bends (ratio ", r.ratio, ")"));
	h.end_scope();
}

FF_TEST_F(test_core_interactions, IxFx, test_custom_bands_wind_vs_fire_and_lightning_vs_barrier) {
	SimHarness& h = H(1);
	h.begin_scope();
	Interactions::add_rule("flame", "t_wind", D({{"bands", A({A({0.0, "amplify"}), A({1.0, "deflect"}), A({2.0, "extinguish"})})}}));
	AgentRef f = _counter("flame", 0.0);
	f->kind = "volume";
	f->cls = "flame";
	f->ch.H = 12.0;
	std::vector<std::string> outs;
	for (double cp : {7.0, 18.0, 28.0}) outs.push_back(Interactions::predict(h.w, *f, *_counter("t_wind", cp)).outcome);
	check(outs == std::vector<std::string>{"amplify", "deflect", "extinguish"}, S("wind vs fire 12: ", outs[0], " ", outs[1], " ", outs[2]));
	AgentRef bolt = Agent::of_volume(h.w, nullptr, nullptr, "lightning", Vec3(), Vec3(0, 0, -1), D({{"E", 24.0}}));
	MatBody* wall = h.w->spawn_body(Mat::Stone, Form::Wall, 120.0, Vec3(0, 0, -3), "test");
	h.w->mass_ledger.ground_taken += 120.0;
	AgentRef wa = Agent::of_body(*h.w, *wall);
	check(wa->ccls == "wall_stone", "Bulwark class");
	near(Interactions::counter_power(*wa), 30.0, 1e-9, "CP = 120 kg x 0.25");
	check(Interactions::predict(h.w, *bolt, *wa).outcome == "ground", "bolt 24 grounds");
	bolt->ch.E = 36.0;
	const IxResult pr = Interactions::predict(h.w, *bolt, *wa);
	check(pr.outcome == "shatter" && pr.band == "partial", "storm bolt 36 shatters (" + pr.outcome + ")");
	h.end_scope();
}

FF_TEST_F(test_core_interactions, IxFx, test_weaken_is_subtractive_and_counters_stack) {
	SimHarness& h = H(1);
	h.begin_scope();
	Interactions::add_rule("boulder", "t_swallow", D({{"outcome", "sink"}, {"partial", "weaken"}, {"fail", "weaken"}, {"partial_at", 0.0}}));
	Interactions::add_rule("boulder", "t_ram", D({{"outcome", "block"}, {"partial", "weaken"}}));
	MatBody* b = _stone(200.0, 11.0);
	AgentRef t = Agent::of_body(*h.w, *b);
	near(Interactions::threat_power(*t, Dict()), 110.0, 1e-9, "boulder 110");
	const IxResult r = Interactions::resolve(*h.w, *t, *_counter("t_swallow", 55.0));
	check(r.outcome == "weaken", "Swallow T3 (55) only weakens a boulder");
	near(b->vel.length(), 5.5, 1e-6, "subtractive: speed x (110 - 55) / 110");
	AgentRef t2 = Agent::of_body(*h.w, *b);
	near(Interactions::threat_power(*t2, Dict()), 55.0, 1e-5, "55 left");
	const IxResult r2 = Interactions::resolve(*h.w, *t2, *_counter("t_ram", 56.0));
	check(r2.outcome == "block" && r2.stopped, "then the Ram Wall stops it (stacking)");
	std::vector<Dict> ixs;
	for (const Dict& e : h.w->events)
		if (e.get("type") == "interaction") ixs.push_back(e);
	check(ixs.size() == 2, S("interaction events (", ixs.size(), ")"));
	const Dict ev = ixs.empty() ? Dict() : ixs.back();
	for (const char* k : {"threat", "counter", "outcome", "band", "ratio", "tp", "cp", "perfect", "pos", "dir", "threat_actor", "counter_actor",
	                      "threat_body", "counter_body", "to"})
		check(ev.has(k), S("interaction field ", k));
	h.end_scope();
}

FF_TEST_F(test_core_interactions, IxFx, test_overwhelm_breaks_the_counter_and_passes_the_rest) {
	SimHarness& h = H(1);
	h.begin_scope();
	MatBody* wall = h.w->spawn_body(Mat::Sand, Form::Wall, 40.0, Vec3(0, 0, 3), "test");
	h.w->mass_ledger.ground_taken += 40.0;
	wall->wall_rise = 1.0;
	MatBody* b = _stone(45.0, 14.0);
	AgentRef th = Agent::of_body(*h.w, *b);
	AgentRef co = Agent::of_body(*h.w, *wall);
	const IxResult r = Interactions::resolve(*h.w, *th, *co);
	check(r.band == "fail" && r.outcome == "overwhelm", "40 kg sand wall (CP 10) vs heave 31.5: fail");
	check(!wall->alive && r.counter_broken, "the wall crumbled");
	near(r.pass_scale, (31.5 - 0.5 * 10.0) / 31.5, 1e-6, "TP - 0.5 CP continues");
	near(b->vel.length(), 14.0 * (31.5 - 5.0) / 31.5, 1e-5, "the stone keeps going, slower");
	h.end_scope();
}

FF_TEST_F(test_core_interactions, IxFx, test_rule_lookup_order_tiers_and_legacy_protection) {
	SimHarness& h = H(1);
	h.begin_scope();
	Interactions::add_rule("solid_light", "t_c", D({{"outcome", "bend"}, {"id", "fam"}}));
	Interactions::add_rule("*", "t_c", D({{"outcome", "pass"}, {"id", "wild"}}));
	check(dstr(Interactions::rule("stone", "t_c"), "id") == "fam", "family beats wildcard");
	Interactions::add_rule("stone", "t_c", D({{"outcome", "sink"}, {"id", "exact"}, {"tiers", A({2, 3})}}));
	check(dstr(Interactions::rule("stone", "t_c", 3), "id") == "exact", "exact at its tiers");
	check(dstr(Interactions::rule("stone", "t_c", 0), "id") == "fam", "outside its tiers it falls through");
	check(dstr(Interactions::rule("water", "t_c"), "id") == "wild", "other families: wildcard");
	check(dstr(Interactions::rule("zzz", "zzz"), "id") == "default", "default rule");
	Interactions::add_rule("stone", "t_c", D({{"outcome", "block"}, {"id", "exact2"}, {"tiers", A({2, 3})}}));
	check(dstr(Interactions::rule("stone", "t_c", 2), "id") == "exact2", "same key + tiers: replaced (deterministic)");
	check(!Interactions::can_add("stone", "wall_stone", D({{"outcome", "pass"}})), "legacy cell cannot be replaced");
	check(!Interactions::can_add("stone", "wall_stone", D({{"outcome", "pass"}, {"tiers", A({3})}})), "not even for some tiers");
	check(Interactions::can_add("sound", "wall_stone", D({{"outcome", "reflect"}})), "a kit may add a new threat class cell");
	check(dbool(Interactions::rule("stone", "guard"), "legacy", false), "legacy guard cell");
	h.end_scope();
	check(!Interactions::has_rule("stone", "t_c"), "scope restored");
}

FF_TEST_F(test_core_interactions, IxFx, test_predict_is_pure_and_move_counters) {
	SimHarness& h = H(1);
	h.begin_scope();
	Moves::register_def("t_shield", D({{"verb", "barrier"}, {"barrier", "held"}, {"mat", "water"}, {"mass", 6.0}, {"counter", D({{"cls", "t_sh"}})}}));
	Moves::register_def("t_gale", D({{"verb", "cone"}, {"counter", D({{"cls", "t_g"}, {"power", A({7, 11, 18, 28})}})}}));
	Interactions::add_rule("stone", "t_g", D({{"eff", 2.0}, {"outcome", "deflect"}, {"partial", "bend"}}));
	MatBody* b = _stone(20.0, 17.0);
	const Vec3 v0 = b->vel;
	const size_t n = h.log.size();
	const size_t ne = h.w->events.size();
	AgentRef t = Agent::of_body(*h.w, *b);
	const IxResult r = h.predict(*t, "t_gale", 3);
	check(r.outcome == "deflect" && is_equal_approx(r.cp, 28.0) && is_equal_approx(r.cp_eff, 56.0), "gale T3 deflects");
	check(h.predict(*t, "t_gale", 0).outcome == "bend", "T0 bends");
	check(is_equal_approx(h.predict(*t, "t_shield", 0).cp, 6.0), "barrier spec power = mass x hardness (held water 1/kg)");
	check(b->vel == v0 && h.w->events.size() == ne && h.log.size() == n, "predict changes nothing");
	h.end_scope();
}

FF_TEST_F(test_core_interactions, IxFx, test_allows_drives_technique_legality) {
	SimHarness& h = H(1);
	MatBody* s = _stone(20.0, 0.0);
	check(Interactions::allows(*s, "grip_stone"), "seize a stone");
	check(!Interactions::allows(*s, "draw_heat"), "no heat to draw from cold stone");
	Thermal::heat(*s, 300.0);
	check(Interactions::allows(*s, "draw_heat"), "hot rock: draw");
	MatBody* m = h.w->spawn_body(Mat::Metal, Form::Chunk, 5.0, Vec3(), "test");
	check(!Interactions::allows(*m, "grip_stone"), "Stone Seize does not take metal");
	check(Interactions::cohesion(2) == 0.8 && Interactions::disrupt_threshold(1) == 10.0, "cohesion / disrupt thresholds");
}
