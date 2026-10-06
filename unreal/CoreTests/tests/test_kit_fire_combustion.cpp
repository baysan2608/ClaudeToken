// Port of game/tests/sim/test_kit_fire_combustion.gd: Fire / Combustion (sub 3): detonations by tier, what blasts do to the
// world (shatter ice, snuff fire fields, disperse clouds), the environment around a blast (a vacuum suppresses it, vapour
// halves it, the inrush after a vacuum collapses boosts it, a fuse in a tornado makes a fire tornado), Spark Mines,
// Reactive Blast, Smother Blast.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Fire/Fire.h"
#include "Combat/Kits/Fire/FireUtil.h"
#include "Sim/Charge.h"
#include "Util/GdUtil.h"

#include <cmath>
#include <map>

using namespace ff;
using namespace fft;

namespace {
struct CombustionFx : FireCase {
	void _hold_strike(ActorState* p, int ticks) {
		h().press(p, "attack");
		h().step(ticks);
		h().release(p, "attack");
		h().step(2);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
};
using EU::keep;
}  // namespace

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_pop_burst_blast_detonation_by_tier) {
	const double dists[4] = {1.6, 6.0, 9.0, 12.0};
	const double radii[4] = {2.0, 2.0, 3.0, 4.5};
	for (int tier = 0; tier < 4; ++tier) {
		const double dist = dists[tier];
		auto [p, r] = duel(3, Sim::EARTH, 5, dist);
		SimHarness& h = this->h();
		r->is_dummy = true;
		const Snap base = snap(*h.w);
		const double f0 = p->focus;
		const Dict pop = Moves::defs().get("pop").as_dict();
		const int ticks = tier == 0 ? 3 : static_cast<int>(Charge::tier_times(pop)[static_cast<size_t>(tier - 1)] * 60.0) + 4;
		_hold_strike(p, ticks);
		const double spent = (f0 - p->focus) * 10.0;
		h.step(45);
		check(r->health < 100.0, S("T", tier, ": the detonation at ", dist, " m hits the rival (", r->health, ")"));
		const std::vector<Dict> bursts = h.filter("fx", [](const Dict& e) { return dstr(e, "fx") == "burst" && dstr(e, "mat") == "blast"; });
		check(!bursts.empty(), S("T", tier, ": a blast burst cue"));
		if (!bursts.empty()) near(dnum(bursts[0], "radius"), radii[tier], 1e-6, S("T", tier, " radius"));
		const double hu = dnum(pop, "heat") + Charge::pgetf(pop, tier, "heat_add", 0.0) * (tier > 0 ? 1.0 : 0.0);
		check(spent >= hu - 1.0, S("T", tier, ": paid ", hu, " HU (", spent, ")"));
		ledgers_ok(base, S("pop T", tier));
		fx_catalogued(S("pop T", tier));
	}
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_blasts_shatter_ice_and_snuff_a_fire_field) {
	auto [p, r] = duel(3, Sim::WATER, 5, 8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef field = keep(FireUtil::spawn_field(*h.w, r->id, p->pos + p->forward() * 6.0, 1.5, 5.0, 200.0));
	h.w->ledger.generated += 200.0;
	const Vec3 side = p->forward().cross(Vec3::Up()).normalized();
	BodyRef ice = keep(h.w->spawn_body(Mat::Water, Form::Chunk, 4.0, p->pos + p->forward() * 6.0 + side * 1.9 + V3(0, 1.0, 0), "test", -5.0));
	ice->gravity_scale = 0.0;
	ice->vel = -p->forward() * 0.6;
	ice->attack_id = h.w->new_attack_id();
	ice->attack_owner = r->id;
	const double e1 = ice->thermal_energy();
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	h.w->ledger.freeze_dump += ice->thermal_energy() - e1;
	const Snap base = snap(*h.w);
	h.aim(p, p->forward());
	_hold_strike(p, 28);   // Burst (T1) at 6 m
	h.step(30);
	const int fid = field->id;
	const int iid = ice->id;
	check(!field->alive && h.any_event("extinguish", [&](const Dict& e) { return is(e, "body", fid); }), "the blast snuffs the fire field");
	check(h.any_event("shatter", [&](const Dict& e) { return is(e, "body", iid); }), "and shatters the ice");
	ledgers_ok(base, "blast vs field and ice");
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_a_null_zone_suppresses_a_detonation) {
	for (const char* tag_c : {"null_bubble", "t_null"}) {
		const std::string tag = tag_c;
		auto [p, r] = duel(3, Sim::AIR, 5, 6.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		h.begin_scope();
		if (tag == "t_null") Interactions::register_tag_class("t_null", "vacuum", "t_null");
		h.spawn_zone(tag, r->pos, 2.5, r, 14.0);
		const Snap base = snap(*h.w);
		_hold_strike(p, 28);
		h.step(30);
		check(h.has_event("blast_suppressed"), tag + ": the blast is suppressed");
		check(r->health == 100.0, tag + ": the rival inside is untouched");
		ledgers_ok(base, "suppressed");
		h.end_scope();
	}
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_vapour_halves_a_blast_and_the_vacuum_inrush_boosts_it) {
	std::map<std::string, double> dmg;
	for (const char* env : {"clear", "fog"}) {
		auto [p, r] = duel(3, Sim::WATER, 5, 6.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		if (std::string(env) == "fog") h.spawn_zone("fog", r->pos, 3.0, r, 0.0);
		_hold_strike(p, 28);
		h.step(30);
		dmg[env] = 100.0 - r->health;
	}
	near(dmg["fog"], dmg["clear"] * 0.5, 0.01, S("mist halves the blast (", dmg["fog"], " vs ", dmg["clear"], ")"));
	// Fuse right after a vacuum well collapses next to it: x1.5.
	auto [p2, r2] = duel(3, Sim::AIR, 5, 6.0);
	SimHarness& h = this->h();
	r2->is_dummy = true;
	MatBody* well = h.spawn_zone("vacuum_well", r2->pos + V3(2.5, 0, 0), 1.5, r2, 12.0);
	h.press(p2, "tech");
	h.step(30);
	h.w->close_zone(*well, "collapsed");
	h.step(2);
	h.release(p2, "tech");
	h.step(6);
	const Dict fe = h.last_event("fuse");
	check(!fe.empty() && arr_has_str(darr(fe, "mods"), "inrush"), S("the inrush boosts the fuse (", fe.get("mods"), ")"));
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_fuse_inside_a_tornado_makes_a_fire_tornado) {
	auto [p, r] = duel(3, Sim::AIR, 5, 7.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef tor = keep(h.spawn_zone("tornado", r->pos + V3(0, 0, 1.5), 2.0, r, 25.0));
	tor->max_life = 6.0;
	const Snap base = snap(*h.w);
	h.press(p, "tech");
	h.step(40);
	h.release(p, "tech");
	h.step(6);
	check(h.has_event("infuse") && dbool(tor->props, "fire", false), "the tornado is set alight");
	const std::vector<BodyRef> f = zones_tagged("fire_field");
	check(f.size() == 1 && dint(f[0]->props, "follow", -1) == tor->id, "a fire field rides the tornado");
	h.step(40);
	ActorState* rr = r;
	check(r->status.has("burning") || h.any_event("status", [&](const Dict& e) { return is(e, "actor", rr->id) && dstr(e, "status") == "burning"; }),
	      "the fire tornado burns");
	ledgers_ok(base, "fire tornado");
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_spark_mine_proximity_and_remote_detonation) {
	auto mine_stuck = [](const Dict& e) { return dbool(e, "mine", false); };
	{
		auto [p, r] = duel(3, Sim::EARTH, 5, 9.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		const Snap base = snap(*h.w);
		h.aim(p, rotated(p->forward(), Vec3::Up(), 0.9));
		h.flick(p, "attack", static_cast<int>(Gesture::Up));
		h.step(3);
		h.release(p, "attack");
		h.until([&]() { return h.any_event("stick", mine_stuck); }, 120);
		std::vector<BodyRef> mines;
		for (const BodyRef& b : bodies_of(Mat::Fire))
			if (dbool(b->props, "mine", false)) mines.push_back(b);
		check(mines.size() == 1, "a mine is stuck on the ground");
		if (mines.size() == 1) {
			r->is_dummy = false;
			r->pos = mines[0]->pos + V3(2.5, 0, 0);
			h.it(r).move = V3(-1, 0, 0);
			h.until([&]() { return h.has_event("ember_pop"); }, 90);
			h.it(r).move = Vec3();
			check(dstr(h.last_event("ember_pop"), "why", "") == "proximity" && r->health < 100.0, "it pops when the rival walks in");
		}
		ledgers_ok(base, "mine proximity");
	}
	// Remote: a second flick up detonates it.
	auto [p, r] = duel(3, Sim::EARTH, 5, 9.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	h.aim(p, rotated(p->forward(), Vec3::Up(), 0.9));
	h.flick(p, "attack", static_cast<int>(Gesture::Up));
	h.step(3);
	h.release(p, "attack");
	h.until([&]() { return h.any_event("stick", mine_stuck); }, 120);
	h.step(40);
	const double f0 = p->focus;
	h.flick(p, "attack", static_cast<int>(Gesture::Up));
	h.step(3);
	h.release(p, "attack");
	h.step(30);
	check(h.any_event("ember_pop", [](const Dict& e) { return dstr(e, "why") == "remote"; }), "a second flick up detonates it");
	check(p->focus >= f0 - 0.01, "the detonation costs nothing");
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_reactive_blast_deflects_light_solids_and_reflects_on_a_perfect) {
	for (bool perfect : {false, true}) {
		auto [p, r] = duel(3, Sim::EARTH, 5, 10.0);
		SimHarness& h = this->h();
		BodyRef st = keep(h.launch_at(p, static_cast<int>(Mat::Stone), 20.0, 17.0, Sim::AMBIENT_C, "", r, 6.0));
		h.w->mass_ledger.ground_taken += 20.0;
		const Snap base = snap(*h.w);
		if (!perfect) h.press(p, "guard");
		const double f0 = p->focus;
		for (int k = 0; k < 30; ++k) {
			if (perfect && (st->pos - p->chest()).length() < 2.4f && !p->guarding) h.press(p, "guard");
			h.step();
		}
		check(p->health == 100.0, S("perfect=", perfect, ": no damage"));
		check(h.has_event("reactive_blast"), S("perfect=", perfect, ": the guard detonated"));
		near(f0 - p->focus, FireCombustion::REACTIVE_COST, 0.5, S("perfect=", perfect, ": 8 Focus per trigger"));
		if (perfect) {
			check(st->attack_owner == p->id, "the stone now flies for the guard (reflected)");
			check(st->vel.dot(r->pos - p->pos) > 0.0f, "back toward the thrower");
		}
		h.release(p, "guard");
		h.step(20);
		ledgers_ok(base, S("reactive perfect=", perfect));
	}
}

FF_TEST_F(test_kit_fire_combustion, CombustionFx, test_smother_blast_snuffs_fields_around_and_jumps) {
	auto [p, r] = duel(3, Sim::EARTH, 5, 8.0);
	(void)r;
	SimHarness& h = this->h();
	std::vector<BodyRef> fields;
	for (int k = 0; k < 3; ++k) {
		fields.push_back(keep(FireUtil::spawn_field(*h.w, 2, p->pos + V3(std::cos(k * 2.0), 0, std::sin(k * 2.0)) * 2.5, 1.2, 6.0, 120.0)));
		h.w->ledger.generated += 120.0;
	}
	const Snap base = snap(*h.w);
	const double y0 = p->pos.y;
	h.press(p, "guard");
	h.step(8);
	h.flick(p, "guard", static_cast<int>(Gesture::Down));
	double top = y0;
	for (int k = 0; k < 40; ++k) {
		h.step();
		top = maxf(top, p->pos.y);
	}
	h.release(p, "guard");
	bool all_out = true;
	for (const BodyRef& f : fields)
		if (f->alive) all_out = false;
	check(all_out, "every fire field within 4 m is snuffed");
	check(top > y0 + 1.2, S("the blast launches the fighter (", top - y0, " m)"));
	h.step(40);
	ledgers_ok(base, "smother");
}
