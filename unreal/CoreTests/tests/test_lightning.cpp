// Port of game/tests/sim/test_lightning.gd: lightning: charge, strike, conduction through the pool / metal plate / puddles,
// barriers, redirect, wet bonus and the "one discharge per bolt" rule (Conduction::discharge). Bystanders share the
// caster's team so they are never auto-targeted: the lock target is always T.
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Conduction.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>
#include <map>

using namespace ff;
using namespace fft;

namespace {
constexpr double BOLT_COST = 22.0;
constexpr double DMG = 24.0;      // Moves.DEFS.lightning.damage
constexpr double BUDGET = 26.0;   // Moves.DEFS.lightning.conduct_budget

struct Lt : HarnessCase {
	ActorState* c = nullptr;
	ActorState* t = nullptr;

	void _world(Vec3 c_pos, const Dict& kit = D({{"lightning", true}})) {
		SimHarness& hh = H(1);
		c = hh.actor("C", c_pos, 0, kit, Sim::FIRE);
	}
	ActorState* _add(const std::string& nm, Vec3 pos, int team, const Dict& kit = Dict(), int elem = Sim::EARTH) {
		return h().actor(nm, pos, team, kit, elem);
	}
	void _warm() {
		h().step(20);   // actors turn to face their targets, surfaces update
		h().log.clear();
	}
	// Hold attack for hold_ticks (>= 39 charges the bolt), release, resolve the strike tick.
	void _strike(int hold_ticks = 48) {
		h().press(c, "attack");
		h().step(hold_ticks);
		h().release(c, "attack");
		h().step(1);
	}
	Dict _bolt() { return h().last_event("lightning"); }
	int _hits_on(const ActorState* a) {
		return h().count_events("hit", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id && ev_s(e, "kind") == "lightning"; });
	}
	static double _lost(const ActorState* a) { return Sim::HEALTH_MAX - a->health; }
	// C charges, T (a Fire fighter with the technique unless overridden) presses guard `lead` ticks before the strike tick.
	void _redirect_trial(int lead, const Dict& kit = D({{"redirect_current", true}}), int elem = Sim::FIRE) {
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1, kit, elem);
		_warm();
		SimHarness& hh = h();
		hh.press(c, "attack");
		hh.step(44);
		hh.press(t, "guard");
		if (lead > 0) {
			hh.step(1);
			hh.step(lead - 1);
			hh.release(c, "attack");
		} else {
			hh.release(c, "attack");
		}
		hh.step(1);
	}
	static std::vector<std::string> strs(const Value& v) {
		std::vector<std::string> out;
		for (const Value& x : v.as_array()) out.push_back(x.to_string());
		return out;
	}
};
}  // namespace

// ---------------------------------------------------------------- charging

FF_TEST_F(test_lightning, Lt, test_charge_ready_after_lightning_min_and_bolt_on_release) {
	_world(V3(0, 0, 6));
	t = _add("T", V3(0, 0, -2), 1);
	_warm();
	SimHarness& h = this->h();
	h.press(c, "attack");
	const double lmin = dnum(Moves::defs().get("fire_attack").as_dict(), "lightning_min");
	const int ticks = static_cast<int>(std::floor(lmin * Sim::HZ)) - 2;
	h.step(ticks);
	check(!h.has_event("charge_ready"), S("not ready before lightning_min (", ticks, " ticks)"));
	h.step(6);
	check(h.has_event("charge_ready"), "ready shortly after lightning_min");
	check(!h.has_event("lightning"), "nothing fires while still holding");
	check(t->health == 100.0, "no damage while charging");
	h.release(c, "attack");
	h.step(1);
	check(h.has_event("lightning"), "releasing the charged attack strikes");
	check(!h.has_event("flare"), "and does not also flare");
}

FF_TEST_F(test_lightning, Lt, test_release_before_lightning_min_is_only_a_flare) {
	_world(V3(0, 0, 6));
	t = _add("T", V3(0, 0, -2), 1);
	_warm();
	_strike(30);   // 0.5 s: past heavy_min (0.4 s) but short of lightning_min (0.65 s)
	SimHarness& h = this->h();
	h.step(30);
	check(h.has_event("flare") && ev_b(h.last_event("flare"), "heavy"), "a heavy flare instead");
	check(!h.has_event("lightning") && !h.has_event("charge_ready"), "no bolt");
	check(t->health == 100.0, "T (8 m away, flare range 6.5 m) untouched");
}

FF_TEST_F(test_lightning, Lt, test_no_bolt_without_the_technique) {
	_world(V3(0, 0, 6), Dict());
	t = _add("T", V3(0, 0, -2), 1);
	_warm();
	_strike(48);
	SimHarness& h = this->h();
	h.step(30);
	check(!h.has_event("charge_ready") && !h.has_event("lightning"), "a fighter without 'lightning' never bolts");
	check(h.has_event("flare"), "the held attack becomes a heavy flare");
	check(t->health == 100.0, "no damage at 8 m");
}

FF_TEST_F(test_lightning, Lt, test_insufficient_focus_means_no_bolt_boundary) {
	// 22 Focus is exactly the bolt cost. Regen is held off (focus_idle reset) so the value is exact.
	for (const auto& cs : {std::pair<double, bool>{22.0, true}, std::pair<double, bool>{21.9, false}, std::pair<double, bool>{15.0, false}}) {
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1);
		_warm();
		c->focus = cs.first;
		c->focus_idle = 0.0;
		_strike(41);   // 0.683 s: past lightning_min, before focus regen resumes
		SimHarness& h = this->h();
		h.step(20);
		const bool fired = h.has_event("lightning");
		check(fired == cs.second, S("focus ", cs.first, ": bolt fired=", fired, ", expected ", cs.second));
		if (cs.second) {
			check(c->focus < 1.0, S("a bolt at exactly the cost spends it all (", c->focus, ")"));
			check(t->health < 100.0, "and hurts T");
		} else {
			check(!h.has_event("charge_ready"), S("focus ", cs.first, ": never reports a ready charge"));
			check(t->health == 100.0, S("focus ", cs.first, ": T untouched"));
			check(c->focus >= 0.0, "focus never negative");
		}
	}
}

// ---------------------------------------------------------------- direct hits

FF_TEST_F(test_lightning, Lt, test_dry_target_hit_directly_exactly_once) {
	_world(V3(0, 0, 6));
	t = _add("T", V3(0, 0, -2), 1);
	ActorState* bystander = _add("B", V3(5, 0, 0), 0);
	_warm();
	SimHarness& h = this->h();
	const double focus0 = c->focus;
	_strike();
	const Dict ev = _bolt();
	const Array hits = darr(ev, "hits");
	check(!ev.empty() && hits.size() == 1 && vint(hits.get(0)) == t->id, "the bolt reports exactly T as hit");
	check(!dbool(ev, "blocked", true), "not blocked");
	near(t->health, 100.0 - DMG, 1e-6, "dry T loses the base damage");
	check(_hits_on(t) == 1, S("one lightning hit on T, got ", _hits_on(t)));
	check(bystander->health == 100.0 && c->health == 100.0, "nobody else is hurt");
	check(!h.has_event("conduct"), "dry ground: nothing to conduct through");
	near(c->focus, focus0 - BOLT_COST, 0.5, S("the bolt costs 22 Focus (", focus0, " -> ", c->focus, ")"));
	// No further damage on later ticks, through the whole action and beyond.
	const double hp0 = t->health;
	h.step(240);
	near(t->health, hp0, 1e-9, "no repeated damage on later ticks");
	check(_hits_on(t) == 1, "still exactly one hit event after 4 s");
	check(h.events("lightning").size() == 1, "exactly one discharge");
	check(c->action == nullptr && c->stun == 0.0, "caster is idle again (no stuck action)");
}

FF_TEST_F(test_lightning, Lt, test_bolt_range_and_aim_limits) {
	// Out of range (16 m > 14 m): the bolt is wasted.
	_world(V3(0, 0, 8));
	t = _add("T", V3(0, 0, -8), 1);
	_warm();
	_strike();
	check(t->health == 100.0, "16 m: out of range");
	check(h().has_event("lightning") && darr(_bolt(), "hits").size() == 0, "the bolt still fires but hits nothing");
	// In range (12 m).
	_world(V3(0, 0, 8));
	t = _add("T", V3(0, 0, -4), 1);
	_warm();
	_strike();
	check(t->health < 100.0, "12 m: in range");
	// Aimed 15 degrees off the target still locks on; 35 degrees off misses.
	for (const auto& cs : {std::pair<double, bool>{15.0, true}, std::pair<double, bool>{35.0, false}}) {
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1);
		_warm();
		const double a = deg_to_rad(cs.first);
		h().aim(c, V3(std::sin(a), 0, -std::cos(a)));
		_strike();
		check((t->health < 100.0) == cs.second, S("aim ", cs.first, " degrees off: hit=", t->health < 100.0, ", expected ", cs.second));
	}
}

FF_TEST_F(test_lightning, Lt, test_wet_target_takes_more_damage_than_dry) {
	std::map<double, double> dmg;
	for (double wet : {0.0, 1.0}) {
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1);
		_warm();
		h().press(c, "attack");
		h().step(48);
		t->wetness = wet;
		h().release(c, "attack");
		h().step(1);
		dmg[wet] = _lost(t);
	}
	near(dmg[0.0], DMG, 1e-6, "dry damage");
	near(dmg[1.0], DMG * 1.5, 1e-6, "soaked damage is 1.5x");
	check(dmg[1.0] > dmg[0.0], "wet takes more than dry");
	// The bonus switches on just above 0.3 wetness (wetness decays ~0.0008 in the strike tick).
	for (const auto& cs : {std::pair<double, bool>{0.3015, true}, std::pair<double, bool>{0.2995, false}}) {
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1);
		_warm();
		h().press(c, "attack");
		h().step(48);
		t->wetness = cs.first;
		h().release(c, "attack");
		h().step(1);
		near(_lost(t), cs.second ? DMG * 1.5 : DMG, 1e-6, S("wetness ", cs.first, ": damage"));
	}
}

// ---------------------------------------------------------------- conduction

FF_TEST_F(test_lightning, Lt, test_pool_conducts_to_bystander_but_not_to_the_metal_plate) {
	_world(V3(2, 0, -1));
	t = _add("T", V3(9.5, 0, -1), 1);
	ActorState* in_pool = _add("B", V3(11, 0, 1), 0);
	ActorState* on_metal = _add("M", V3(-9, 0.02, -1), 0);
	ActorState* on_stone = _add("S", V3(6.0, 0, -1), 0);   // beside the pool, dry
	_warm();
	SimHarness& h = this->h();
	check(t->in_water && in_pool->in_water, "setup: T and B stand in the pool");
	check(on_metal->surface == "metal" && !on_metal->in_water, "setup: M stands on the metal plate");
	check(!on_stone->in_water, "setup: S stands beside the pool");
	_strike();
	near(_lost(t), DMG * 1.5, 1e-6, "T (soaked) takes the direct hit");
	check(_lost(in_pool) > 0.0, S("the bystander in the pool is hurt by conduction (", _lost(in_pool), ")"));
	check(on_metal->health == 100.0, "the metal plate is disconnected from the pool: M unhurt");
	check(on_stone->health == 100.0, "a dry bystander next to the pool is unhurt");
	check(c->health == 100.0, "caster unhurt");
	check(_hits_on(t) == 1 && _hits_on(in_pool) == 1, "each victim is hit exactly once");
	const Dict conduct = h.last_event("conduct");
	check(!conduct.empty() && strs(conduct.get("nodes")) == std::vector<std::string>{"pool"}, "graph reached only the pool");
	// Conducted damage is the budget (single victim) x soaked bonus.
	near(_lost(in_pool), BUDGET * 1.5, 1e-6, "single conducted victim gets the whole budget, soaked");
	h.step(180);
	check(_hits_on(t) == 1 && _hits_on(in_pool) == 1, "no later ticks add damage");
}

FF_TEST_F(test_lightning, Lt, test_metal_plate_conducts_between_actors_on_it_but_not_into_the_pool) {
	_world(V3(-3.0, 0, 5));
	t = _add("T", V3(-9, 0.02, -1), 1);
	ActorState* plate_mate = _add("M", V3(-10.5, 0.02, 1), 0);
	ActorState* in_pool = _add("P", V3(9.5, 0, -1), 0);
	_warm();
	check(t->surface == "metal" && plate_mate->surface == "metal", "setup: both stand on the plate");
	_strike();
	check(_lost(t) >= DMG - 1e-6, S("T takes the direct hit (", _lost(t), ")"));
	check(_lost(plate_mate) > 0.0, "the other fighter on the plate is hurt through the metal");
	check(in_pool->health == 100.0, "the pool is a separate circuit: P unhurt");
}

FF_TEST_F(test_lightning, Lt, test_puddle_overlapping_the_metal_plate_connects_a_bystander) {
	_world(V3(-3.0, 0, 5));
	SimHarness& h = this->h();
	t = _add("T", V3(-9, 0.02, -1), 1);
	// 20 kg puddle (radius ~0.8 m) centred 0.4 m beyond the plate edge (x = -6): it overlaps the plate.
	MatBody* puddle = h.w->spawn_body(Mat::Water, Form::Puddle, 20.0, V3(-5.6, 0, 1.0), "scenario");
	puddle->update_radius_puddle();
	ActorState* wet_b = _add("B", V3(-5.2, 0, 1.0), 0);   // inside that puddle
	ActorState* dry_b = _add("D", V3(-4.2, 0, 3.0), 0);   // on dry stone close by
	MatBody* far_puddle = h.w->spawn_body(Mat::Water, Form::Puddle, 20.0, V3(2.0, 0, 8.0), "scenario");
	far_puddle->update_radius_puddle();
	ActorState* far_b = _add("F", V3(2.0, 0, 8.0), 0);    // inside a puddle that touches nothing
	_warm();
	const std::string pnode = "puddle:" + itos(puddle->id);
	check(wet_b->surface == "puddle" && far_b->surface == "puddle", "setup: B and F stand in puddles");
	check(Conduction::actor_surface_node(*h.w, *wet_b) == pnode, "setup: B is a node of the puddle");
	_strike();
	check(_lost(t) > 0.0, "T on the plate is struck");
	check(_lost(wet_b) > 0.0, S("B in the overlapping puddle is connected to the plate and hurt (", _lost(wet_b), ")"));
	check(dry_b->health == 100.0, "a dry bystander is not hurt");
	check(far_b->health == 100.0, "a puddle that touches neither plate nor pool is an isolated node");
	const Dict conduct = h.last_event("conduct");
	const Array nodes = darr(conduct, "nodes");
	check(!conduct.empty() && arr_has_str(nodes, "metal") && arr_has_str(nodes, pnode), "graph: metal -> puddle");
	check(!arr_has_str(nodes, "pool"), "graph does not include the pool");
}

FF_TEST_F(test_lightning, Lt, test_puddle_chain_to_the_pool_is_limited_to_max_hops) {
	_world(V3(3, 0, 6));
	SimHarness& h = this->h();
	t = _add("T", V3(10, 0, -1), 1);
	const int max_hops = dint(Moves::defs().get("lightning").as_dict(), "max_hops");
	std::vector<ActorState*> victims;
	for (int k = 1; k < max_hops + 3; ++k) {
		const double x = 6.6 - 1.4 * static_cast<double>(k - 1);
		MatBody* p = h.w->spawn_body(Mat::Water, Form::Puddle, 20.0, V3(x, 0, -1), "scenario");
		p->update_radius_puddle();
		victims.push_back(_add(S("V", k), V3(x, 0, -1), 0));
	}
	_warm();
	_strike();
	check(_lost(t) > 0.0, "T in the pool takes the direct hit");
	for (size_t k = 0; k < victims.size(); ++k) {
		const int hops = static_cast<int>(k) + 1;   // puddle k+1 is k+1 hops from the pool
		if (hops <= max_hops) {
			check(_lost(victims[k]) > 0.0, S("puddle ", hops, " (", hops, " hops) is reached"));
			check(_hits_on(victims[k]) == 1, S("puddle ", hops, " victim is hit once"));
		} else {
			check(victims[k]->health == 100.0, S("puddle ", hops, " (", hops, " hops) is beyond max_hops=", max_hops));
		}
	}
}

FF_TEST_F(test_lightning, Lt, test_frozen_puddle_does_not_conduct) {
	_world(V3(3, 0, 6));
	SimHarness& h = this->h();
	t = _add("T", V3(10, 0, -1), 1);
	MatBody* ice = h.w->spawn_body(Mat::Water, Form::Puddle, 20.0, V3(6.6, 0, -1), "scenario");
	ice->update_radius_puddle();
	ice->liquid = 0.0;
	ice->temp = -5.0;
	ice->phase = Phase::Frozen;
	ActorState* on_ice = _add("V", V3(6.6, 0, -1), 0);
	_warm();
	_strike();
	check(_lost(t) > 0.0, "T in the pool is struck");
	check(on_ice->health == 100.0, "ice never conducts: the actor standing on it is unhurt");
}

FF_TEST_F(test_lightning, Lt, test_conducted_damage_is_a_bounded_budget_split_between_victims) {
	_world(V3(2, 0, -1));
	t = _add("T", V3(9.5, 0, -1), 1);
	std::vector<ActorState*> bystanders;
	for (int i = 0; i < 8; ++i)
		bystanders.push_back(_add(S("B", i), V3(8.0 + static_cast<double>(i % 4) * 1.3, 0, -3.5 + static_cast<double>(i / 4) * 2.2), 0));
	_warm();
	for (ActorState* b : bystanders) check(b->in_water, "setup: " + b->name + " stands in the pool");
	_strike();
	double total = 0.0;
	int hit = 0;
	for (ActorState* b : bystanders) {
		const double lost = _lost(b);
		check(_hits_on(b) <= 1, S(b->name, " hit at most once (", _hits_on(b), ")"));
		if (lost > 0.0) {
			++hit;
			total += lost / 1.5;   // undo the soaked bonus
		}
	}
	check(hit >= 1, "someone was hit by conduction");
	check(total <= BUDGET + 1e-6, S("conducted damage (", total, " before the wet bonus) stays within the 26 budget"));
	check(hit < static_cast<int>(bystanders.size()), S("the budget runs out before 8 victims are all hurt (", hit, " hit)"));
	const double per_share = hit > 0 ? total / static_cast<double>(hit) : 0.0;
	check(per_share >= 5.0 - 1e-6, S("each conducted hit is at least MIN_SHARE (", per_share, ")"));
	near(_lost(t), DMG * 1.5, 1e-6, "direct hit unaffected by the budget");
}

// ---------------------------------------------------------------- barriers

FF_TEST_F(test_lightning, Lt, test_earth_wall_blocks_the_bolt) {
	_world(V3(0, 0, 3));
	t = _add("T", V3(0, 0, -7), 1);
	_warm();
	SimHarness& h = this->h();
	h.press(t, "guard");   // Earth guard raises a wall in front of T
	h.step(12);
	check(t->wall_body >= 0, "setup: T raised a wall");
	_strike();
	check(dbool(_bolt(), "blocked", false), "the bolt is stopped by the wall");
	check(t->health == 100.0, "T unhurt behind the wall");
	check(_hits_on(t) == 0, "no hit event");
	check(!h.has_event("conduct"), "nothing conducted");
}

FF_TEST_F(test_lightning, Lt, test_earth_wall_blocks_the_bolt_at_every_range) {
	// The wall is thin (0.56 m): a barrier test sampling the bolt at a few points can slip through it.
	std::string leaks;
	for (double d = 5.0; d <= 13.9; d += 0.5) {
		_world(V3(0, 0, -7.0 + d));
		t = _add("T", V3(0, 0, -7), 1);
		_warm();
		h().press(t, "guard");
		h().step(12);
		_strike();
		if (!dbool(_bolt(), "blocked", false) || t->health < 100.0) leaks += ftos(d, 1) + " ";
	}
	check(leaks.empty(), "bolt passed through the earth wall at caster distances [" + leaks + "] m");
}

FF_TEST_F(test_lightning, Lt, test_wall_only_blocks_once_it_has_risen) {
	// The wall rises in ~0.14 s; the bolt travels at chest height, so a half-risen wall is too low to stop it.
	for (const auto& cs : {std::pair<int, bool>{1, false}, std::pair<int, bool>{5, false}, std::pair<int, bool>{14, true}}) {
		_world(V3(0, 0, 3));
		t = _add("T", V3(0, 0, -7), 1);
		_warm();
		SimHarness& h = this->h();
		h.press(c, "attack");
		h.step(46);
		h.press(t, "guard");
		h.step(cs.first - 1);
		h.release(c, "attack");
		h.step(1);   // strike lands cs.first ticks after the guard press
		const bool blocked = dbool(_bolt(), "blocked", false);
		check(blocked == cs.second, S("wall pressed ", cs.first, " ticks before the strike: blocked=", blocked, ", expected ", cs.second));
	}
}

FF_TEST_F(test_lightning, Lt, test_cover_wall_between_caster_and_target_blocks_the_bolt) {
	_world(V3(-3.75, 0, 4));
	t = _add("T", V3(-3.75, 0, -5), 1);
	_warm();
	_strike();
	check(dbool(_bolt(), "blocked", false), S("the cover wall stops the bolt (T lost ", _lost(t), ")"));
	check(t->health == 100.0, "T is protected by the cover wall");
}

FF_TEST_F(test_lightning, Lt, test_tall_arena_solid_blocks_the_bolt) {
	// Control for the barrier mechanism: the 3.2 m pillar in the south-west corner is tall enough.
	_world(V3(-13, 0, 9));
	t = _add("T", V3(-13, 0, 15), 1);
	_warm();
	_strike();
	check(dbool(_bolt(), "blocked", false), "the pillar blocks the bolt");
	check(t->health == 100.0, "T unhurt behind the pillar");
	// Same distance without the pillar in between: hit.
	_world(V3(-8, 0, 9));
	t = _add("T", V3(-8, 0, 15), 1);
	_warm();
	_strike();
	check(!dbool(_bolt(), "blocked", true) && t->health < 100.0, "clear line: T is hit");
}

// ---------------------------------------------------------------- redirect

FF_TEST_F(test_lightning, Lt, test_perfect_timed_fire_guard_redirects_the_bolt_to_the_caster) {
	for (int lead : {0, 1, 5, 10}) {
		_redirect_trial(lead);
		SimHarness& h = this->h();
		check(h.has_event("lightning_redirect"), S("lead ", lead, ": redirect happened"));
		check(h.events("lightning_redirect").size() == 1, S("lead ", lead, ": exactly one redirect per bolt"));
		near(_lost(c), DMG * 0.8, 1e-6, S("lead ", lead, ": the caster takes 80% of the bolt"));
		check(t->health == 100.0, S("lead ", lead, ": the redirector takes nothing"));
		check(_hits_on(c) == 1 && _hits_on(t) == 0, S("lead ", lead, ": one lightning hit, on the caster"));
		const double hp0 = c->health;
		h.step(120);
		near(c->health, hp0, 1e-9, S("lead ", lead, ": no further damage afterwards"));
	}
}

FF_TEST_F(test_lightning, Lt, test_late_guard_does_not_redirect) {
	for (int lead : {11, 12, 20, 60}) {   // 11 ticks = 0.183 s > the 0.18 s perfect window
		_redirect_trial(lead);
		SimHarness& h = this->h();
		check(!h.has_event("lightning_redirect"), S("lead ", lead, ": no redirect"));
		check(c->health == 100.0, S("lead ", lead, ": the caster is unharmed"));
		// A normal (late) guard is no answer to lightning: the bolt goes through minus the guard's CP 10 (24 -> 14).
		check(t->health < 100.0 - DMG * 0.5 && t->health > 100.0 - DMG * 0.7, S("lead ", lead, ": E - CP through a normal guard (health ", t->health, ")"));
		check(!h.has_event("block"), S("lead ", lead, ": no clean block"));
	}
}

FF_TEST_F(test_lightning, Lt, test_no_redirect_without_the_technique_or_the_fire_element) {
	_redirect_trial(3, Dict(), Sim::FIRE);
	check(!h().has_event("lightning_redirect"), "Fire guard without redirect_current: no redirect");
	check(c->health == 100.0, "caster unharmed");
	check(t->health == 100.0, "a perfect guard still negates the bolt for the defender");
	_redirect_trial(3, D({{"redirect_current", true}}), Sim::EARTH);
	check(!h().has_event("lightning_redirect"), "Earth guard with the technique: no redirect");
	check(c->health == 100.0, "caster unharmed");
	_redirect_trial(3, D({{"redirect_current", true}}), Sim::WATER);
	check(!h().has_event("lightning_redirect"), "Water guard with the technique: no redirect");
	check(c->health == 100.0, "caster unharmed");
}

FF_TEST_F(test_lightning, Lt, test_guard_mashing_never_redirects) {
	// Guard, release, guard again quickly: the second guard is "mashed" and has no perfect window.
	{
		_world(V3(0, 0, 6));
		t = _add("T", V3(0, 0, -2), 1, D({{"redirect_current", true}}), Sim::FIRE);
		_warm();
		SimHarness& h = this->h();
		h.press(c, "attack");
		h.step(30);
		h.press(t, "guard");
		h.step(1);
		h.release(t, "guard");
		h.step(3);
		h.press(t, "guard");   // buffered until the first guard's recovery ends
		const int started = h.until([&]() { return t->guarding; }, 30);
		check(started > 0, "second guard started");
		check(t->action != nullptr && dbool(t->action->data, "mashed", false), "the second guard is flagged as mashed");
		h.step(1);
		h.release(c, "attack");
		h.step(1);   // strike lands ~2 ticks after the second guard began
		check(!h.has_event("lightning_redirect"), "no redirect for a mashed guard");
		check(c->health == 100.0, "caster unharmed");
	}
	// Same but with a gap longer than GUARD_MASH_LOCK: the window is valid again.
	_world(V3(0, 0, 6));
	t = _add("T", V3(0, 0, -2), 1, D({{"redirect_current", true}}), Sim::FIRE);
	_warm();
	SimHarness& h = this->h();
	h.press(c, "attack");
	h.step(20);
	h.press(t, "guard");
	h.step(1);
	h.release(t, "guard");
	h.step(static_cast<int>(Moves::GUARD_MASH_LOCK * Sim::HZ) + 6);
	h.press(t, "guard");
	h.step(1);
	h.step(1);
	h.release(c, "attack");
	h.step(1);
	check(h.has_event("lightning_redirect"), "after the mash lock expires a fresh perfect guard redirects");
}
