// Port of game/tests/sim/test_kit_water_ice.gd: Water / Ice (sub 1, docs/MOVESET.md §7.6): the Frost Shard ladder, Icicle
// Volley, Rime Path (freezes puddles and water waves, crusts lava, walkable strip over the pool), Hoarfrost Fan (wet
// targets freeze), Ice Wall + Flash Freeze, Glacier Shove, Frost Floor, Freeze-Draw + Shatter, Ice Glide and Skate.
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Water/WaterUtil.h"
#include "Sim/Conduction.h"
#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
constexpr int UP = static_cast<int>(Gesture::Up);
constexpr int DOWN = static_cast<int>(Gesture::Down);
constexpr int SIDE = static_cast<int>(Gesture::Side);

struct WI : WaterCase {
	std::pair<ActorState*, ActorState*> _ice_duel(double dist = 10.0, int rival_element = Sim::EARTH, uint64_t seed_value = 3) {
		return duel(1, rival_element, seed_value, dist);
	}
	void _flick_tap(ActorState* p, int gesture, int hold_ticks = 2) {
		h().flick(p, "attack", gesture);
		h().step(hold_ticks);
		h().release(p, "attack");
	}
	void _flick_hold(ActorState* p, int gesture, double secs) {
		h().flick(p, "attack", gesture);
		h().step(static_cast<int>(secs * 60.0));
		h().release(p, "attack");
	}
	MatBody* _lava_wave(double mass, Vec3 pos, Vec3 dir, ActorState* owner) {
		SimHarness& hh = h();
		MatBody* b = hh.w->spawn_body(Mat::Stone, Form::Wave, mass, pos, "test");
		hh.w->mass_ledger.ground_taken += mass;
		hh.w->ledger.generated += Thermal::heat(*b, mass * (Sim::STONE_C * 980.0 + Sim::STONE_LATENT));
		Thermal::update_phase(*b);
		b->wave_dir = dir;
		b->wave_budget = 12.0;
		b->vel = dir * 7.5f;
		b->attack_id = hh.w->new_attack_id();
		b->attack_owner = owner->id;
		b->hit_set.add(owner->id);
		return b;
	}
	BodyRef _raise_wall(ActorState* w, int ticks = 18) {
		h().press(w, "guard");
		h().step(ticks);
		return keep(h().w->get_body(w->wall_body));
	}
	bool _ix(const std::function<bool(const Dict&)>& pred) { return h().any_event("interaction", pred); }
	static double flat(Vec3 a, Vec3 b) { return static_cast<double>(Vec2(a.x - b.x, a.z - b.z).length()); }
	static double flat_speed(Vec3 v) { return static_cast<double>(Vec2(v.x, v.z).length()); }
	static IxCtx site(const std::string& s) {
		IxCtx c;
		c.site = s;
		return c;
	}
	std::vector<BodyRef> water_tagged(const std::string& tag) {
		std::vector<BodyRef> out;
		for (const BodyRef& b : bodies_of(Mat::Water))
			if (b->tag == tag) out.push_back(b);
		return out;
	}
};

bool is_eq(const Dict& e, const char* k, const char* v) { return ev_s(e, k) == v; }
}  // namespace

// ---------------------------------------------------------------- strike ladder

FF_TEST_F(test_kit_water_ice, WI, test_frost_shard_ladder_t0_to_t3) {
	struct Res {
		int tier, reached;
		double mass;
		std::string tag;
		bool frozen;
	};
	std::vector<Res> results;
	const int holds[4] = {2, 30, 66, 112};   // tap, T1 (0.4 s), T2 (1.0 s), T3 (1.8 s)
	for (int tier = 0; tier < 4; ++tier) {
		auto [w, r] = _ice_duel(10.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		const Snap base = snap(*h.w);
		const double m0 = h.w->mass_ledger.moisture_taken;
		h.log.clear();
		h.press(w, "attack");
		h.step(holds[tier]);
		const int reached = w->action != nullptr ? w->action->tier() : 0;
		h.release(w, "attack");
		h.until([&]() { return h.has_event("launch"); }, 40);
		const Dict l = h.last_event("launch");
		check(!l.empty(), S("T", tier, " launches (tier reached ", reached, ")"));
		MatBody* b = h.w->get_body(ev_i(l, "body", -1));
		if (b != nullptr) {
			results.push_back({tier, reached, b->mass, b->tag, b->phase == Phase::Frozen});
			check(b->phase == Phase::Frozen, S("T", tier, " is ice"));
		}
		h.step(40);
		check(h.w->mass_ledger.moisture_taken - m0 > 0.9, S("T", tier, " booked ambient moisture (", h.w->mass_ledger.moisture_taken - m0, " kg)"));
		h.step(200);
		ledgers_ok(base, S("Frost Shard T", tier));
	}
	std::string desc;
	for (const Res& x : results) desc += S("[", x.tier, ", ", x.reached, ", ", x.mass, ", ", x.tag, ", ", x.frozen, "] ");
	check(results.size() == 4, "all four tiers fired: " + desc);
	if (results.size() == 4) {
		check(is_equal_approx(results[0].mass, 1.0) && results[0].tag == "needle", "T0: 1 kg needle");
		check(is_equal_approx(results[1].mass, 4.0) && results[1].tag == "lance", "T1: 4 kg Ice Lance");
		check(is_equal_approx(results[2].mass, 8.0) && results[2].tag == "spear", "T2: 8 kg Glacier Spear");
		check(is_equal_approx(results[3].mass, 15.0) && results[3].tag == "comet", "T3: 15 kg Frost Comet");
	}
}

FF_TEST_F(test_kit_water_ice, WI, test_frost_shard_chills_and_glacier_spear_pierces_and_stands) {
	auto [w, r] = _ice_duel(9.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	h->press(w, "attack");
	h->step(3);
	h->release(w, "attack");
	h->step(80);
	check(Status::has(*r, "chilled") || h->has_event("status", "status", Value("chilled")), "the needle chills");
	// T2 spear
	std::tie(w, r) = _ice_duel(9.0);
	h = &this->h();
	r->is_dummy = true;
	h->press(w, "attack");
	h->step(66);
	h->release(w, "attack");
	h->step(80);
	check(h->has_event("pierce") || h->has_event("hit"), "the spear hits and pierces");
	check(h->has_event("stick"), "then stands as an ice spike");
	const std::vector<BodyRef> spikes = water_tagged("spear");
	check(spikes.size() == 1 && spikes[0]->static_body, "one standing spike");
	h->step(200);
	check(water_tagged("spear").empty(), "the spike is gone after ~3 s");
}

FF_TEST_F(test_kit_water_ice, WI, test_frost_comet_shatters_into_six_shards) {
	auto [w, r] = _ice_duel(8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	h.press(w, "attack");
	h.step(112);
	h.release(w, "attack");
	h.step(100);
	check(h.has_event("shatter"), "the comet shatters");
	int n = 0;
	for (const BodyRef& b : bodies_of(Mat::Water))
		if (b->phase == Phase::Frozen || b->form == Form::Puddle) ++n;
	check(n >= 4, S("into several pieces (", n, ")"));
}

FF_TEST_F(test_kit_water_ice, WI, test_icicle_volley_counts_by_tier) {
	const int want[4] = {3, 5, 7, 12};
	const double secs[4] = {0.05, 0.5, 1.1, 1.9};
	for (int tier = 0; tier < 4; ++tier) {
		auto [w, r] = _ice_duel(10.0);
		(void)r;
		SimHarness& h = this->h();
		w->focus = 100.0;
		h.log.clear();
		h.flick(w, "attack", UP);
		h.step(static_cast<int>(secs[tier] * 60.0) + 2);
		h.release(w, "attack");
		h.step(30);
		const size_t n = h.events("launch").size();
		check(static_cast<int>(n) == want[tier], S("T", tier, ": ", want[tier], " needles (got ", n, ")"));
	}
}

// ---------------------------------------------------------------- ground: Rime Path

FF_TEST_F(test_kit_water_ice, WI, test_rime_path_leaves_a_slick_ice_floor_and_the_owner_keeps_grip) {
	auto [w, r] = _ice_duel(12.0);
	(void)r;
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	_flick_tap(w, DOWN);
	h.step(70);
	const std::vector<BodyRef> floors = zones_tagged("ice_floor");
	check(floors.size() >= 5, S("a strip of ice_floor zones (", floors.size(), ")"));
	if (!floors.empty()) check(dnum(floors[0]->props, "friction", 1.0) < 0.2 && floors[0]->tier == 0, "slick and tier-tagged");
	h.step(240);
	check(zones_tagged("ice_floor").empty(), "the strip melts away after its life");
	ledgers_ok(base, "Rime Path");
}

FF_TEST_F(test_kit_water_ice, WI, test_rime_path_freezes_a_puddle_and_a_frozen_puddle_stops_conduction) {
	auto [w, r] = _ice_duel(12.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->kit = D({{"lightning", true}});
	r->is_dummy = true;
	// A puddle chain from the pool edge to the middle of the Rime Path; the rival's bolt strikes the pool.
	MatBody* pud = h.w->spawn_body(Mat::Water, Form::Stream, 4.0, V3(0, 0.3, 0), "test");
	h.w->mass_ledger.moisture_taken += 4.0;
	h.w->_water_to_puddle(*pud);
	const BodyRef live = keep(h.w->spawn_body(Mat::Water, Form::Stream, 6.0, V3(0.0, 0.3, -2.0), "test"));
	h.w->mass_ledger.moisture_taken += 6.0;
	h.w->_water_to_puddle(*live);
	check(live->alive && live->form == Form::Puddle && live->phase == Phase::Liquid, "a liquid puddle in the path");
	ActorState* victim = h.w->add_actor("V", V3(0.0, 0, -2.0), 0, Dict(), Sim::EARTH);
	h.intents[victim->id] = ActorIntent();
	victim->is_dummy = true;
	h.step(2);
	const Snap base = snap(*h.w);
	_flick_tap(w, DOWN);
	h.step(80);
	const bool frozen = live->alive && live->phase == Phase::Frozen;
	check(frozen, S("the Rime Path froze the puddle (phase ", live->alive ? Sim::phase_name(live->phase) : "gone", ")"));
	check(_ix([](const Dict& e) { return is_eq(e, "threat", "puddle") && is_eq(e, "counter", "rime"); }), "interaction puddle x rime");
	// No conduction through ice: a fighter standing on the frozen puddle is not part of the graph.
	victim->pos = V3(live->pos.x, 0, live->pos.z);
	check(Conduction::actor_surface_node(*h.w, *victim).empty(), "standing on a frozen puddle: no conduction node");
	ledgers_ok(base, "frozen puddle");
}

FF_TEST_F(test_kit_water_ice, WI, test_rime_path_turns_a_water_wave_into_an_ice_ridge) {
	// The rival's Tidal Rush meets our Rime Path: the wave becomes a standing ice ridge (a WALL).
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(5.0, 0, 4.5), 0, Dict(), Sim::WATER);
	ActorState* r = h.actor("R", V3(5.0, 0, -7.0), 1, Dict(), Sim::WATER);
	w->subs[Sim::WATER] = 1;
	h.step(20);
	const Snap base = snap(*h.w);
	h.flick(w, "attack", DOWN);
	h.step(34);
	h.release(w, "attack");
	BodyRef rime;
	for (int k = 0; k < 40; ++k) {
		h.step();
		for (const BodyRef& b : h.w->bodies)
			if (b->alive && b->tag == "rime" && b->form == Form::Wave) rime = b;
		if (rime != nullptr) break;
	}
	check(rime != nullptr, "our Rime Path is out");
	if (rime == nullptr) return;
	// The rival's wave (a Tidal Rush launched at us), spawned 5 m ahead of ours.
	const BodyRef wave = keep(h.w->spawn_body(Mat::Water, Form::Wave, 10.0, rime->pos + V3(0, 0, -5.0), "test"));
	h.w->mass_ledger.moisture_taken += 10.0;
	wave->tag = "water_wave";
	wave->wave_dir = V3(0, 0, 1);
	wave->wave_budget = 14.0;
	wave->wave_width = 2.4;
	wave->power = 24.0;
	wave->props.set("speed", 9.0);
	wave->props.set("knock", 6.0);
	wave->props.set("lift", 3.0);
	wave->attack_id = h.w->new_attack_id();
	wave->attack_owner = r->id;
	wave->hit_set.add(r->id);
	BodyRef ridge;
	for (int k = 0; k < 120; ++k) {
		h.step();
		if (wave->alive && wave->form == Form::Wall) {
			ridge = wave;
			break;
		}
	}
	check(ridge != nullptr, "the water wave became a WALL");
	if (ridge == nullptr) return;
	check(ridge->tag == "ridge" && ridge->phase == Phase::Frozen, "tagged ridge and frozen (" + ridge->describe() + ")");
	check(_ix([](const Dict& e) {
		      return is_eq(e, "threat", "water_wave") && is_eq(e, "counter", "rime") && is_eq(e, "outcome", "transform") && is_eq(e, "to", "ridge");
	      }),
	      "interaction water_wave x rime -> ridge");
	h.step(60);
	check(ridge->alive && ridge->wall_rise > 0.9, "the ridge stands");
	check(Interactions::counter_class(*ridge, h.w) == "wall_ice", "and counts as an ice wall");
	h.step(600);
	check(!ridge->alive, "it melts away after its time");
	ledgers_ok(base, "ridge");
}

FF_TEST_F(test_kit_water_ice, WI, test_rime_path_crusts_a_lava_front) {
	auto [w, r] = _ice_duel(12.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	_flick_hold(w, DOWN, 1.9);   // T3: power 30 x 1.2
	h.step(10);
	const BodyRef lava = keep(_lava_wave(20.0, w->pos + V3(0, 0, -9.0), V3(0, 0, 1), r));
	bool crust = false;
	for (int k = 0; k < 160; ++k) {
		h.step();
		if (_ix([](const Dict& e) { return is_eq(e, "threat", "lava_wave") && is_eq(e, "counter", "rime"); })) {
			crust = true;
			break;
		}
	}
	check(crust, "the lava front meets the rime wave");
	h.step(120);
	check(lava->alive && lava->liquid < 0.5, S("the front is crusted (liquid ", lava->liquid, ")"));
	ledgers_ok(base, "crust");
}

FF_TEST_F(test_kit_water_ice, WI, test_rime_path_over_the_pool_is_a_walkable_strip) {
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(5.5, 0, -1.0), 0, Dict(), Sim::WATER);
	ActorState* r = h.actor("R", V3(-4.0, 0, -1.0), 1, Dict(), Sim::EARTH);
	r->is_dummy = true;
	w->subs[Sim::WATER] = 1;
	w->facing = kPi * 0.5;   // toward +x (the pool)
	h.step(5);
	w->facing = kPi * 0.5;
	h.aim(w, V3(1, 0, 0));
	h.it(w).aim_dir = V3(1, 0, 0);
	h.it(w).aim_active = true;
	ActorState* c = h.w->add_actor("C", V3(10.0, 0.0, -1.0), 1, Dict(), Sim::EARTH);
	h.intents[c->id] = ActorIntent();
	c->is_dummy = true;
	h.step(2);
	const Snap base = snap(*h.w);
	_flick_hold(w, DOWN, 0.5);
	h.step(50);
	std::vector<BodyRef> strip;
	for (const BodyRef& z : zones_tagged("ice_floor"))
		if (h.w->arena.in_pool(z->pos.x, z->pos.z)) strip.push_back(z);
	check(strip.size() >= 2, S("ice_floor zones over the pool (", strip.size(), ")"));
	if (strip.size() >= 2) {
		const BodyRef z = strip[0];
		const double top = static_cast<double>(z->pos.y) + dnum(z->props, "walk_height");
		check(std::fabs(top) < 0.05, S("its surface is at ground level (top ", top, ")"));
		// A fighter on the strip walks over the pool: not in the water, no conduction node.
		c->pos = V3(z->pos.x, 0.0, z->pos.z);
		h.step(4);
		check(!c->in_water, S("standing on the strip: not in the water (y ", c->pos.y, ", surface ", c->surface, ")"));
		check(Conduction::actor_surface_node(*h.w, *c).empty(), "no pool conduction node on ice");
	}
	h.step(400);
	ledgers_ok(base, "pool strip");
}

// ---------------------------------------------------------------- sweep: Hoarfrost

FF_TEST_F(test_kit_water_ice, WI, test_hoarfrost_freezes_wet_targets_and_chills_dry_ones) {
	auto [w, r] = _ice_duel(4.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	r->wetness = 1.0;
	const Snap base = snap(*h->w);
	_flick_tap(w, SIDE);
	h->step(40);
	check(Status::has(*r, "frozen") || h->has_event("status", "status", Value("frozen")), "a wet target freezes solid (rooted)");
	check(Status::rooted(*r) || h->has_event("status", "status", Value("frozen")), "rooted");
	std::tie(w, r) = _ice_duel(4.0);
	h = &this->h();
	r->is_dummy = true;
	r->wetness = 0.0;
	_flick_tap(w, SIDE);
	h->step(40);
	check(!h->has_event("status", "status", Value("frozen")), "a dry target is not frozen");
	check(h->has_event("status", "status", Value("chilled")), "but chilled");
	ledgers_ok(base, "Hoarfrost");
}

FF_TEST_F(test_kit_water_ice, WI, test_hoarfrost_freezes_a_puddle_and_a_stream_in_the_air) {
	auto [w, r] = _ice_duel(5.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const BodyRef pud = keep(h.w->spawn_body(Mat::Water, Form::Stream, 3.0, w->pos + V3(0, 0.3, -2.5), "test"));
	h.w->mass_ledger.moisture_taken += 3.0;
	h.w->_water_to_puddle(*pud);
	const BodyRef stream = keep(h.w->spawn_body(Mat::Water, Form::Blob, 2.0, w->pos + V3(0, 1.2, -3.0), "test"));
	h.w->mass_ledger.moisture_taken += 2.0;
	stream->vel = Vec3();
	stream->gravity_scale = 0.0;
	stream->attack_id = h.w->new_attack_id();
	stream->attack_owner = r->id;
	const Snap base = snap(*h.w);
	_flick_tap(w, SIDE);
	h.step(30);
	check(pud->phase == Phase::Frozen, "the puddle froze");
	check(stream->phase == Phase::Frozen || !stream->alive, S("the stream froze mid-air (", Sim::phase_name(stream->phase), ")"));
	h.step(200);
	ledgers_ok(base, "Hoarfrost bodies");
}

// ---------------------------------------------------------------- guard: Ice Wall

FF_TEST_F(test_kit_water_ice, WI, test_ice_wall_masses_and_tiers) {
	auto [w, r] = _ice_duel(10.0);
	SimHarness* h = &this->h();
	w->pos = V3(-5, 0, 6);
	h->step(3);
	const Snap base = snap(*h->w);
	const double m0 = h->w->mass_ledger.moisture_taken;
	BodyRef wall = _raise_wall(w);
	check(wall != nullptr && wall->form == Form::Wall && wall->tag == "ice", "an ice WALL body (" + (wall ? wall->describe() : std::string("none")) + ")");
	if (wall == nullptr) return;
	near(wall->mass, 50.0, 0.01, "50 kg away from water");
	near(wall->mass * Materials::hardness(*wall), 22.0, 0.01, "CP 22");
	check(wall->is_water() && wall->phase == Phase::Frozen, "frozen water");
	near(h->w->mass_ledger.moisture_taken - m0, 50.0, 1e-6, "booked as ambient moisture");
	// held 1.0 s and 1.8 s: thicker (65 / 80 kg)
	h->step(50);
	check(wall->mass >= 64.9, S("held 1.1 s: 65 kg (", wall->mass, ")"));
	h->step(60);
	check(wall->mass >= 79.9, S("held 1.9 s: 80 kg (", wall->mass, ")"));
	h->release(w, "guard");
	h->step(80);
	check(!wall->alive, "the wall sinks when released");
	ledgers_ok(base, "Ice Wall");
	// beside the pool: 70 kg
	std::tie(w, r) = _ice_duel(10.0);
	h = &this->h();
	w->pos = V3(5.0, 0, 3.5);
	h->step(3);
	wall = _raise_wall(w);
	check(wall != nullptr && std::fabs(wall->mass - 70.0) < 0.01, S("beside the pool: 70 kg (", wall ? wall->mass : 0.0, ")"));
}

FF_TEST_F(test_kit_water_ice, WI, test_ice_wall_blocks_a_stone_and_insulates_a_bolt_until_a_storm_bolt) {
	auto [w, r] = _ice_duel(8.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->kit = D({{"lightning", true}});
	r->is_dummy = true;
	const BodyRef wall = _raise_wall(w);
	check(wall != nullptr, "wall up");
	if (wall == nullptr) return;
	const double hp0 = w->health;
	h.launch_at(w, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 6.0);
	h.step(40);
	check(w->health == hp0, "the stone never reaches the fighter");
	check(wall->alive, "and the wall holds");
	// Insulator: a T1 bolt (E 24 <= 1.5 x 22) is grounded.
	Dict def = D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	r->pos = w->pos + V3(0, 0, -8.0);
	int aid = h.w->new_attack_id();
	Dict out = Conduction::discharge(*h.w, *r, w->chest(), def, aid, true);
	h.step();
	check(dbool(out, "blocked") && w->health == hp0, "E 24 is blocked by the ice (insulator, 1.5 x CP 22 = 33)");
	check(_ix([](const Dict& e) { return is_eq(e, "counter", "wall_ice") && is_eq(e, "threat", "lightning") && is_eq(e, "outcome", "ground"); }),
	      "interaction lightning x wall_ice -> ground");
	// A T2 storm bolt (E 36) shatters it and continues with E - 0.5 CP.
	def.set("E", 36.0);
	def.set("damage", 36.0);
	aid = h.w->new_attack_id();
	out = Conduction::discharge(*h.w, *r, w->chest(), def, aid, true);
	h.step();
	check(!wall->alive || wall->wall_damage >= 1.0, "a storm bolt (E 36) shatters the wall");
	check(_ix([](const Dict& e) { return is_eq(e, "counter", "wall_ice") && is_eq(e, "threat", "lightning") && is_eq(e, "outcome", "shatter"); }),
	      "interaction -> shatter");
}

FF_TEST_F(test_kit_water_ice, WI, test_flash_freeze_perfect_guard_freezes_a_water_stream_and_a_wave_becomes_a_ridge) {
	auto [w, r] = _ice_duel(8.0, Sim::WATER);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const BodyRef wall = _raise_wall(w);
	check(wall != nullptr, "wall up");
	if (wall == nullptr) return;
	const Snap base = snap(*h.w);
	// A perfect guard at the moment of contact: the wall is a counter with perfect = true.
	const BodyRef stream = keep(h.w->spawn_body(Mat::Water, Form::Blob, 6.0, wall->pos + V3(0, 0.9, -0.6), "test"));
	h.w->mass_ledger.moisture_taken += 6.0;
	stream->vel = V3(0, 0, 14.0);
	stream->gravity_scale = 0.0;
	stream->attack_id = h.w->new_attack_id();
	stream->attack_owner = r->id;
	AgentRef counter = Agent::of_body(*h.w, *wall);
	counter->actor = w;
	counter->perfect = true;
	AgentRef th = Agent::of_body(*h.w, *stream, w);
	const IxResult res = Interactions::resolve(*h.w, *th, *counter, site("wall"));
	check(res.outcome == "transform" && res.to == "ice", "Flash Freeze: the stream freezes (" + res.outcome + " -> " + res.to + ")");
	check(stream->phase == Phase::Frozen && stream->attack_id == 0, "frozen solid and dropped (inert)");
	// The same perfect guard turns a water wave into a ridge.
	const BodyRef wave = keep(h.w->spawn_body(Mat::Water, Form::Wave, 10.0, wall->pos + V3(0, 0, -0.8), "test"));
	h.w->mass_ledger.moisture_taken += 10.0;
	wave->tag = "water_wave";
	wave->wave_dir = V3(0, 0, 1);
	wave->wave_budget = 10.0;
	wave->wave_width = 2.4;
	wave->power = 24.0;
	wave->props.set("speed", 9.0);
	wave->attack_id = h.w->new_attack_id();
	wave->attack_owner = r->id;
	AgentRef th2 = Agent::of_body(*h.w, *wave, w);
	const IxResult res2 = Interactions::resolve(*h.w, *th2, *counter, site("wave_wall"));
	check(res2.outcome == "transform" && res2.to == "ridge" && wave->form == Form::Wall && wave->tag == "ridge",
	      "Flash Freeze: the wave becomes an ice ridge (" + wave->describe() + ")");
	// Without the perfect timing the plain wall just blocks (the stream splashes).
	const BodyRef stream2 = keep(h.w->spawn_body(Mat::Water, Form::Blob, 3.0, wall->pos + V3(0, 0.9, -0.6), "test"));
	h.w->mass_ledger.moisture_taken += 3.0;
	stream2->vel = V3(0, 0, 14.0);
	stream2->gravity_scale = 0.0;
	stream2->attack_id = h.w->new_attack_id();
	stream2->attack_owner = r->id;
	counter->perfect = false;
	AgentRef th3 = Agent::of_body(*h.w, *stream2, w);
	const IxResult res3 = Interactions::resolve(*h.w, *th3, *counter, site("wall"));
	check(res3.outcome == "block" && stream2->phase == Phase::Liquid, "plain guard: block, water stays liquid");
	h.release(w, "guard");
	h.step(300);
	ledgers_ok(base, "Flash Freeze");
}

FF_TEST_F(test_kit_water_ice, WI, test_ice_wall_melts_under_fire) {
	auto [w, r] = _ice_duel(8.0, Sim::FIRE);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	const BodyRef wall = _raise_wall(w);
	check(wall != nullptr, "wall up");
	if (wall == nullptr) return;
	// Strong heat on the wall (a smelter / lava): it melts and slumps into a puddle.
	for (int k = 0; k < 6; ++k) {
		const double e = h.w->heat_body(*wall, 50.0);   // 300 HU: melts the 50 kg wall (177 HU) and warms it
		h.w->ledger.generated += e;
	}
	h.step(30);
	check(wall->form == Form::Puddle || !wall->alive || wall->phase == Phase::Liquid, "the wall melted (" + wall->describe() + ")");
	check(wall->form != Form::Wall, "it is no longer a wall");
	h.release(w, "guard");
	h.step(300);
	ledgers_ok(base, "melt");
}

// ---------------------------------------------------------------- push / sink

FF_TEST_F(test_kit_water_ice, WI, test_glacier_shove_slides_the_wall_into_the_rival) {
	auto [w, r] = _ice_duel(7.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	const BodyRef wall = _raise_wall(w);
	check(wall != nullptr, "wall up");
	if (wall == nullptr) return;
	const Vec3 p0 = wall->pos;
	h.flick(w, "guard", UP);
	h.step(6);
	h.release(w, "guard");
	h.step(80);
	const double moved = static_cast<double>(wall->pos.distance_to(p0));
	check(wall->alive && moved > 3.0, S("the wall slid (", moved, " m)"));
	check(r->health < 100.0 || h.has_event("hit", "actor", Value(r->id)), "and knocked the rival back");
	h.step(400);
	ledgers_ok(base, "Glacier Shove");
}

FF_TEST_F(test_kit_water_ice, WI, test_frost_floor_is_slick_for_enemies_and_grippy_for_you) {
	auto [w, r] = _ice_duel(4.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(12);
	h.flick(w, "guard", DOWN);
	h.step(20);
	h.release(w, "guard");
	h.step(10);
	const std::vector<BodyRef> fl = zones_tagged("ice_floor");
	check(!fl.empty() && std::fabs(fl[0]->zone_radius - 3.0) < 0.01, "a 3 m ice floor");
	check(Status::has(*w, "icegrip"), "the owner keeps their grip (status icegrip)");
	r->pos = w->pos + V3(0, 0, -2.0);
	h.step(2);
	check(h.w->_friction_at(*r) < 0.2, S("enemies slide (friction ", h.w->_friction_at(*r), ")"));
	check(h.w->_friction_at(*w) > 0.8, S("you don't (", h.w->_friction_at(*w), ")"));
	h.step(400);
	check(zones_tagged("ice_floor").empty(), "the floor ends after 5 s");
	ledgers_ok(base, "Frost Floor");
}

// ---------------------------------------------------------------- technique

FF_TEST_F(test_kit_water_ice, WI, test_freeze_draw_grows_an_ice_block_and_shatter_fans_six_shards) {
	auto [w, r] = _ice_duel(8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	h.press(w, "tech");
	h.step(60);
	const BodyRef held = keep(h.w->held(*w));
	check(held != nullptr && held->phase == Phase::Frozen && held->is_water(), "an ice block in hand (" + (held ? held->describe() : std::string("none")) + ")");
	if (held == nullptr) return;
	h.step(90);
	check(held->mass > 6.0 && held->mass <= 12.01, S("it grew (", held->mass, " kg, cap 12)"));
	h.press(w, "attack");
	h.step(3);
	h.release(w, "attack");
	h.step(3);
	h.release(w, "tech");
	h.step(30);
	check(h.events("launch").size() >= 5, S("Shatter: a fan of shards (", h.events("launch").size(), " launches)"));
	h.step(300);
	ledgers_ok(base, "Freeze-Draw");
}

FF_TEST_F(test_kit_water_ice, WI, test_freeze_draw_seizes_incoming_ice) {
	auto [w, r] = _ice_duel(9.0, Sim::WATER);
	SimHarness& h = this->h();
	const BodyRef shard = keep(h.w->spawn_body(Mat::Water, Form::Shard, 4.0, w->pos + V3(0, 1.2, -6.0), "test"));
	h.w->mass_ledger.moisture_taken += 4.0;
	WaterUtil::freeze_body(*h.w, shard.get());
	shard->vel = V3(0, 0, 14.0);
	shard->gravity_scale = 0.0;
	shard->attack_id = h.w->new_attack_id();
	shard->attack_owner = r->id;
	h.step(2);
	h.press(w, "tech");
	h.step(40);
	check(shard->controller == w->id, "the incoming ice shard is seized (REC)");
	h.release(w, "tech");
	h.step(200);
}

// ---------------------------------------------------------------- evade

FF_TEST_F(test_kit_water_ice, WI, test_ice_glide_and_skate_across_the_pool) {
	auto [w0, r0] = _ice_duel(10.0);
	(void)r0;
	{
		SimHarness& h = this->h();
		const Vec3 p0 = w0->pos;
		h.it(w0).move = V3(1, 0, 0);
		h.press(w0, "evade");
		h.step(1);
		h.it(w0).move = Vec3();
		h.step(23);
		const double d = flat(w0->pos, p0);
		check(d > 3.0 && d < 6.5, S("Ice Glide covers about 5 m (", d, ")"));
		check(zones_tagged("ice_floor").size() >= 2, "on a strip of ice");
		h.step(120);
	}
	// Skate: hold evade, run east over the pool.
	SimHarness& h = H(3);
	ActorState* w = h.actor("W", V3(3.0, 0, -1.0), 0, Dict(), Sim::WATER);
	ActorState* r = h.actor("R", V3(-8.0, 0, -1.0), 1, Dict(), Sim::EARTH);
	r->is_dummy = true;
	w->subs[Sim::WATER] = 1;
	h.step(5);
	const Snap base = snap(*h.w);
	h.it(w).move = V3(1, 0, 0);
	h.press(w, "evade");
	h.it(w).evade_held = true;
	bool in_pool_dry = false;
	double max_speed = 0.0;
	for (int k = 0; k < 160; ++k) {
		h.step();
		max_speed = maxf(max_speed, flat_speed(w->vel));
		if (h.w->arena.in_pool(w->pos.x, w->pos.z) && !w->in_water && w->grounded) in_pool_dry = true;
		if (w->pos.x > 13.5f) break;
	}
	check((w->action != nullptr && w->action->id == "skate") || max_speed > 0.0, "morphed into Skate");
	check(max_speed > 6.0, S("skating faster than a run (", max_speed, " m/s)"));
	check(in_pool_dry, S("skated over the pool without falling in (x ", w->pos.x, ")"));
	h.it(w).evade_held = false;
	h.step(120);
	check(zones_tagged("ice_floor").empty(), "the ice under the skater is gone");
	ledgers_ok(base, "Skate");
}
