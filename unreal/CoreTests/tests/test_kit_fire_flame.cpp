// Port of game/tests/sim/test_kit_fire_flame.gd: Fire / Flame (sub 0): the legacy kit stays exact and gains Fire Column /
// Inferno, Fireball (water quenches it, wind feeds it), Fire Line, the extended Heat Sink, Backdraft, Ground Heat, SCORCH
// ("melt the wall") and Rocket Hop.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Sim/Charge.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct FlameFx : FireCase {
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

FF_TEST_F(test_kit_fire_flame, FlameFx, test_fire_column_t2_and_inferno_t3_leave_fire_fields) {
	for (int tier : {2, 3}) {
		auto [p, r] = duel(0, Sim::EARTH, 5, 6.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		const double f0 = p->focus;
		const Dict fa = Moves::defs().get("fire_attack").as_dict();
		_hold_strike(p, static_cast<int>(Charge::tier_times(fa)[static_cast<size_t>(tier - 1)] * 60.0) + 4);
		const Dict fl = h.last_event("flare");
		check(!fl.empty() && dint(fl, "tier", 0) == tier, S("T", tier, ": the strike released at its tier (", fl, ")"));
		check(h.has_event("fire_column"), S("T", tier, ": fire_column event"));
		const std::vector<BodyRef> fields = zones_tagged("fire_field");
		check(fields.size() == 1, S("T", tier, ": one fire field (", fields.size(), ")"));
		if (fields.size() == 1) {
			check(fields[0]->heat_payload > 50.0, S("T", tier, ": the field holds paid heat (", fields[0]->heat_payload, " HU)"));
			near(fields[0]->zone_radius, tier == 2 ? 1.5 : 2.6, 1e-6, S("T", tier, ": field radius"));
		}
		check(r->health < 100.0 - (tier == 2 ? 15.0 : 20.0), S("T", tier, ": the rival in range is hit hard (", r->health, ")"));
		check(r->status.has("burning"), S("T", tier, ": and burns"));
		const double cost = Charge::pgetf(fa, tier, "col_hu", 0.0) / Sim::HU_PER_FOCUS;
		check(f0 - p->focus >= cost - 0.5, S("T", tier, ": paid ", cost * 10.0, " HU (", f0 - p->focus, " Focus spent)"));
		h.step(300);
		check(zones_tagged("fire_field").empty(), S("T", tier, ": the field burns out"));
		ledgers_ok(base, S("T", tier, " column"));
		fx_catalogued(S("column T", tier));
	}
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_legacy_lightning_flag_still_bolts_on_flame) {
	for (int hold : {48, 100}) {
		auto [p, r] = duel(0, Sim::EARTH, 5, 8.0, D({{"lightning", true}}));
		(void)r;
		_hold_strike(p, hold);
		check(h().has_event("lightning") && !h().has_event("fire_column"), S("hold ", hold, " ticks with the lightning flag: the bolt"));
	}
	auto pr2 = duel(0, Sim::EARTH, 5, 8.0);
	_hold_strike(pr2.first, 48);
	check(h().has_event("flare", "heavy", Value(true)) && !h().has_event("lightning"), "no flag, 0.8 s: the legacy blaze");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_fireball_bursts_on_the_rival_and_water_quenches_it) {
	{
		auto [p, r] = duel(0, Sim::EARTH, 5, 9.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		h.flick(p, "attack", static_cast<int>(Gesture::Up));
		h.step(3);
		h.release(p, "attack");
		h.until([&]() { return !bodies_of(Mat::Fire).empty(); }, 40);
		const std::vector<BodyRef> fb = bodies_of(Mat::Fire);
		check(fb.size() == 1 && fb[0]->tag == "fireball", "a fireball body");
		if (fb.size() == 1) check(fb[0]->heat_payload > 100.0, S("carrying the paid heat (", fb[0]->heat_payload, " HU)"));
		h.until([&]() { return h.has_event("fire_burst"); }, 90);
		check(h.has_event("fire_burst") && r->health < 100.0, S("it bursts on the rival (hp ", r->health, ")"));
		h.step(60);
		ledgers_ok(base, "fireball");
	}
	// Water in the way quenches it (booked boil).
	auto [p, r] = duel(0, Sim::EARTH, 5, 9.0);
	SimHarness& h = this->h();
	BodyRef wb = keep(h.w->spawn_body(Mat::Water, Form::Blob, 4.0, (p->chest() + r->chest()) * 0.5, "test"));
	wb->gravity_scale = 0.0;
	wb->static_body = true;
	const Snap base = snap(*h.w);
	h.flick(p, "attack", static_cast<int>(Gesture::Up));
	h.step(3);
	h.release(p, "attack");
	h.until([&]() { return h.has_event("extinguish") || h.has_event("fire_burst"); }, 90);
	check(h.has_event("steam_block"), "the water takes the fireball's heat (steam)");
	check(h.has_event("extinguish") && !h.has_event("fire_burst"), "the fireball is quenched before it bursts");
	check(wb->mass < 4.0, S("some water boiled away (", wb->mass, " kg)"));
	h.step(30);
	ledgers_ok(base, "fireball quenched");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_fireball_held_by_a_wind_grip_is_fed_20_percent) {
	SimHarness& h = H(4);
	h.begin_scope();
	Moves::register_def("t_wind_grip", D({{"element", 3}, {"sub", 1}, {"slot", "tech"}, {"verb", "grip"}, {"ccls", "grip_wind"}, {"startup", 0.05},
	                                      {"active", 0.05}, {"recovery", 0.2}, {"reach", 8.0}, {"cone", 70.0}, {"base", 0.95}, {"speed", 16.0},
	                                      {"damage", 6.0}, {"balance", 10.0}}));
	Moves::bind(3, 1, "tech", "t_wind_grip");
	MatBody probe;
	probe.mat = Mat::Fire;
	probe.tag = "fireball";
	if (!Interactions::allows(probe, "grip_wind"))
		Interactions::add_rule("flame", "grip_wind",
		                       D({{"outcome", "reclaim"}, {"bands", A({Value(A({Value(0.0), Value("reclaim")}))})}, {"full_at", 0.0}, {"id", "t_wind_grip_fire"}}));
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::AIR);
	ActorState* o = h.actor("O", V3(0, 0, -6), 1, Dict(), Sim::FIRE);
	o->is_dummy = true;
	a->subs[3] = 1;
	h.step(20);
	BodyRef fb = keep(h.w->spawn_body(Mat::Fire, Form::Chunk, 0.5, a->chest() + a->forward() * 3.0, "test"));
	fb->tag = "fireball";
	fb->heat_payload = 200.0;
	fb->gravity_scale = 0.0;
	fb->static_body = false;
	h.w->ledger.generated += 200.0;
	const Snap base = snap(*h.w);
	h.press(a, "tech");
	h.step(12);
	check(fb->controller == a->id, "the wind grip holds the fireball");
	const double before = fb->heat_payload;
	h.release(a, "tech");
	h.step(2);
	check(h.has_event("fed"), "released from the wind: it is fed");
	const Dict fed = h.last_event("fed");
	if (!fed.empty()) near(dnum(fed, "add"), before * 0.2, before * 0.03, "+20 % heat");
	h.step(80);
	ledgers_ok(base, "fed fireball");
	h.end_scope();
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_fire_line_runs_leaves_a_trail_and_dies_on_a_puddle) {
	{
		auto [p, r] = duel(0, Sim::EARTH, 5, 10.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		h.flick(p, "attack", static_cast<int>(Gesture::Down));
		h.step(3);
		h.release(p, "attack");
		h.step(30);
		check(!h.events("spawn").empty() && zones_tagged("fire_field").size() >= 1, "the line drops burning patches");
		ActorState* rr = r;
		h.until([&]() { return h.any_event("hit", [&](const Dict& e) { return is(e, "actor", rr->id); }); }, 60);
		check(r->health < 100.0, "it reaches the rival 10 m away");
		h.step(240);
		ledgers_ok(base, "fire line");
	}
	// A puddle on its way puts it out.
	auto [p, r] = duel(0, Sim::EARTH, 5, 10.0);
	SimHarness& h = this->h();
	MatBody* pd = h.w->spawn_body(Mat::Water, Form::Puddle, 6.0, V3(p->pos.x, 0.0, p->pos.z - 4.0), "test");
	pd->update_radius_puddle();
	pd->radius = 1.2;
	const Snap base = snap(*h.w);
	h.flick(p, "attack", static_cast<int>(Gesture::Down));
	h.step(3);
	h.release(p, "attack");
	h.step(80);
	check(h.any_event("extinguish", [](const Dict& e) { return dstr(e, "by", "") == "water"; }), "the puddle douses the line");
	check(r->health == 100.0, S("the rival behind the puddle is safe (", r->health, ")"));
	ledgers_ok(base, "fire line doused");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_perfect_flame_guard_draws_300_hu_from_a_magma_blob_and_melts_ice) {
	{
		auto [p, r] = duel(0, Sim::EARTH, 5, 10.0);
		SimHarness& h = this->h();
		BodyRef blob = keep(h.launch_at(p, static_cast<int>(Mat::Stone), 20.0, 15.0, 1100.0, "", r, 6.0));
		Thermal::heat(*blob, 200.0);
		Thermal::update_phase(*blob);
		h.w->ledger.generated += blob->thermal_energy();
		check(Interactions::classify(*blob) == "magma", "a magma blob (" + Interactions::classify(*blob) + ")");
		const Snap base = snap(*h.w);
		const double e0 = blob->thermal_energy();
		h.step(14);
		h.press(p, "guard");
		h.until([&]() { return h.has_event("heat_sink"); }, 30);
		const Dict hs = h.last_event("heat_sink");
		check(!hs.empty() && absf(dnum(hs, "gain") - 300.0) < 1.0, S("Heat Sink drew 300 HU (", hs.get("gain"), ")"));
		check(p->heat_reserve > 280.0, S("into the reserve (", p->heat_reserve, ")"));
		check(blob->thermal_energy() <= e0 - 299.0, "the blob lost it (it crusts mid-air)");
		h.step(40);
		h.release(p, "guard");
		h.step(20);
		ledgers_ok(base, "heat sink");
	}
	// Ice: a perfect guard melts the shard before it lands.
	auto [p, r] = duel(0, Sim::EARTH, 5, 10.0);
	SimHarness& h = this->h();
	BodyRef ice = keep(h.launch_at(p, static_cast<int>(Mat::Water), 4.0, 18.0, -5.0, "", r, 6.0));
	const double e1 = ice->thermal_energy();
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	h.w->ledger.freeze_dump += ice->thermal_energy() - e1;
	const Snap base = snap(*h.w);
	h.step(16);
	h.press(p, "guard");
	h.until([&]() { return h.has_event("heat_sink"); }, 30);
	check(!ice->alive || ice->phase == Phase::Liquid, S("the ice melted (", ice->alive ? ice->describe() : std::string("gone"), ")"));
	h.step(30);
	h.release(p, "guard");
	h.step(10);
	ledgers_ok(base, "heat sink ice");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_backdraft_returns_the_reserve_and_ground_heat_boils_puddles) {
	{
		auto [p, r] = duel(0, Sim::EARTH, 5, 4.0);
		SimHarness& h = this->h();
		p->heat_reserve = 300.0;
		const Snap base = snap(*h.w);
		h.press(p, "guard");
		h.step(10);
		h.flick(p, "guard", static_cast<int>(Gesture::Up));
		h.step(30);
		const Dict bd = h.last_event("backdraft");
		check(!bd.empty() && dnum(bd, "hu") > 280.0, S("Backdraft releases the reserve (", bd.get("hu"), ")"));
		check(p->heat_reserve < 1.0 && r->health < 90.0, S("reserve spent (", p->heat_reserve, "), rival burned (", r->health, ")"));
		h.release(p, "guard");
		h.step(40);
		ledgers_ok(base, "backdraft");
	}
	auto [p, r] = duel(0, Sim::EARTH, 5, 6.0);
	(void)r;
	SimHarness& h = this->h();
	p->heat_reserve = 200.0;
	BodyRef pd = keep(h.w->spawn_body(Mat::Water, Form::Puddle, 2.0, p->pos + V3(0.8, 0, 0), "test"));
	pd->update_radius_puddle();
	const Snap base = snap(*h.w);
	const double wm = h.w->water_mass();
	h.press(p, "guard");
	h.step(10);
	h.flick(p, "guard", static_cast<int>(Gesture::Down));
	h.step(40);
	check(!pd->alive || pd->mass < 0.1, "the puddle at the feet boiled away");
	check(p->heat_reserve < 1.0 && h.has_event("vent"), "the reserve went into the ground");
	near(h.w->water_mass(), wm, 1e-6, "water mass conserved (vapour booked)");
	h.release(p, "guard");
	h.step(20);
	ledgers_ok(base, "ground heat");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_scorch_slumps_a_bulwark_in_about_one_and_a_half_seconds) {
	auto [p, r] = duel(0, Sim::EARTH, 5, 8.0, D({{"magma", true}, {"heat_draw", true}}));
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef wall = keep(bulwark(r, V3(0, 0, p->pos.z - 3.5), p->facing));
	const Snap base = snap(*h.w);
	const Dict pv = h.w->tech_preview(*p, p->forward());
	check(dstr(pv, "mode") == "SCORCH" && dint(pv, "body") == wall->id, S("the technique offers SCORCH on the wall (", pv, ")"));
	h.press(p, "tech");
	const int64_t t0 = h.w->tick;
	h.until([&]() { return h.has_event("slump"); }, 200);
	const double dt = static_cast<double>(h.w->tick - t0) / 60.0;
	check(h.has_event("slump"), "the wall slumped");
	check(dt > 1.3 && dt < 1.8, S("in about 1.5 s (", dt, " s)"));
	const Dict sl = h.last_event("slump");
	MatBody* face = h.w->get_body(ev_i(sl, "body", -1));
	check(face != nullptr && face->alive && face->liquid >= 0.5 && face->is_stone(), "the molten face body exists");
	if (face != nullptr) {
		near(face->mass, 30.0, 1e-6, "25 % of the wall");
		check((face->pos - wall->pos).dot(p->pos - wall->pos) > 0.0f, "on the caster's side");
	}
	check(!wall->alive && h.has_event("wall_crumble"), "the rest crumbled");
	h.release(p, "tech");
	h.step(30);
	ledgers_ok(base, "scorch");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_rocket_hop_hovers_and_lands) {
	auto [p, r] = duel(0, Sim::EARTH, 5, 8.0);
	(void)r;
	SimHarness& h = this->h();
	const double y0 = p->pos.y;
	h.press(p, "evade");
	h.it(p).evade_held = true;
	double top = y0;
	for (int k = 0; k < 60; ++k) {
		h.step();
		top = maxf(top, p->pos.y);
	}
	h.it(p).evade_held = false;
	check(h.any_event("action", [](const Dict& e) { return dstr(e, "move") == "rocket_hop"; }), "held evade morphs into Rocket Hop");
	check(top > y0 + 1.8, S("it hops up (", top - y0, " m)"));
	h.until([&]() { return p->grounded && p->action == nullptr; }, 120);
	check(p->grounded && absf(p->pos.y - y0) < 0.05, "and lands again");
}

FF_TEST_F(test_kit_fire_flame, FlameFx, test_fireball_into_a_tornado_makes_a_fire_tornado_and_a_vacuum_snuffs_it) {
	// Test-local zone tags (the Air kit's own tornado / bubble zones add their own capture and cells on top).
	for (const char* tag_c : {"t_tornado", "t_null"}) {
		const std::string tag = tag_c;
		auto [p, r] = duel(0, Sim::AIR, 5, 8.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		h.begin_scope();
		Interactions::register_tag_class("t_tornado", "tornado", "t_tornado");
		Interactions::register_tag_class("t_null", "vacuum", "t_null");
		BodyRef z = keep(h.spawn_zone(tag, (p->pos + r->pos) * 0.5, 1.8, r, 25.0));
		z->max_life = 5.0;
		const Snap base = snap(*h.w);
		h.flick(p, "attack", static_cast<int>(Gesture::Up));
		h.step(3);
		h.release(p, "attack");
		h.step(40);
		if (tag == "t_tornado") {
			check(h.has_event("infuse") && dbool(z->props, "fire", false), "the tornado becomes a fire tornado");
			bool rides = false;
			for (const BodyRef& f : zones_tagged("fire_field"))
				if (dint(f->props, "follow", -1) == z->id) rides = true;
			check(rides, "its fire rides the tornado");
		} else {
			check(h.any_event("extinguish", [](const Dict& e) { return dstr(e, "by", "") == "vacuum"; }), "the vacuum snuffs the fireball");
			check(!h.has_event("fire_burst"), "no burst");
		}
		ledgers_ok(base, "fireball into " + tag);
		h.end_scope();
	}
}
