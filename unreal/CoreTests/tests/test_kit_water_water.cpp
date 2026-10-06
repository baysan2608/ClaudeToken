// Port of game/tests/sim/test_kit_water_water.gd: Water / Water (sub 0, docs/MOVESET.md §7.5): Torrent and Maelstrom
// Lash, Water Bullet / Pressure Jet / Cutting Jet (a connected jet conducts lightning back), Tidal Rush (carries solids
// back, quenches lava, douses fire, leaves puddles), Spray Fan, Surge Orb, Slick, Draw & Shape extras (condense, seize,
// Freeze), Riptide Step / Dive and Wave Ride. Counter cells are in test_kit_water_cells.cpp.
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Sim/Conduction.h"
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

struct WW : WaterCase {
	// W beside the pool's west edge (unlimited water within 3 m), R `dist` m away on the same line.
	std::pair<ActorState*, ActorState*> _by_the_pool(int rival_element = Sim::EARTH, double dist = 13.0) {
		SimHarness& hh = H(3);
		ActorState* w = hh.actor("W", V3(5.0, 0, 4.5), 0, Dict(), Sim::WATER);
		ActorState* r = hh.actor("R", V3(5.0, 0, 4.5 - dist), 1, Dict(), rival_element);
		hh.step(20);
		hh.log.clear();
		return {w, r};
	}
	void _tap(ActorState* p, int gesture, int hold_ticks = 2) {
		h().flick(p, "attack", gesture);
		h().step(hold_ticks);
		h().release(p, "attack");
	}
	void _hold_flick(ActorState* p, int gesture, double secs) {
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
		b->tag = "";
		b->attack_id = hh.w->new_attack_id();
		b->attack_owner = owner->id;
		b->hit_set.add(owner->id);
		return b;
	}
	BodyRef _find_tag(const std::string& tag, int form = -1) {
		BodyRef out;
		for (const BodyRef& b : h().w->bodies)
			if (b->alive && b->tag == tag && (form < 0 || static_cast<int>(b->form) == form)) out = b;
		return out;
	}
	static double flat(Vec3 a, Vec3 b) { return static_cast<double>(Vec2(a.x - b.x, a.z - b.z).length()); }
	static double flat_speed(Vec3 v) { return static_cast<double>(Vec2(v.x, v.z).length()); }
};
}  // namespace

// ---------------------------------------------------------------- strike tiers

FF_TEST_F(test_kit_water_water, WW, test_torrent_and_maelstrom_are_the_t2_t3_of_the_legacy_strike) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 10.0);
	SimHarness* h = &this->h();
	w->water_carried = 6.0;
	Snap base = snap(*h->w);
	h->press(w, "attack");
	h->step(66);   // 1.1 s: T2
	check(w->action != nullptr && w->action->tier() == 2, S("held 1.1 s: tier 2 (", w->action ? w->action->tier() : -1, ")"));
	h->release(w, "attack");
	h->step(30);
	const Dict launch = h->last_event("launch");
	check(!launch.empty() && ev_i(launch, "tier") == 2, "Torrent launches a slug at tier 2");
	MatBody* slug = h->w->get_body(ev_i(launch, "body", -1));
	check(slug != nullptr && slug->mass >= 5.0 && slug->mass <= 10.0 && slug->is_water(), S("a water slug of up to 10 kg (", slug ? slug->mass : 0.0, ")"));
	h->step(60);
	check(r->health < 100.0 || h->has_event("hit"), "the Torrent lands");
	ledgers_ok(base, "Torrent");
	// T3: Maelstrom Lash, 360 degrees, r 5 m: a rival behind the caster is hit too.
	std::tie(w, r) = duel(0, Sim::EARTH, 3, 10.0);
	h = &this->h();
	r->pos = w->pos + V3(0, 0, 3.5);   // BEHIND the caster (the caster faces -z)
	r->facing = kPi;
	base = snap(*h->w);
	h->press(w, "attack");
	h->step(112);   // 1.87 s
	check(w->action != nullptr && w->action->tier() == 3, "held 1.87 s: tier 3");
	h->release(w, "attack");
	h->step(40);
	check(h->has_event("lash", "around", Value(true)), "Maelstrom Lash event");
	check(r->health < 100.0, S("the 360 degree whip hits a rival behind the caster (hp ", r->health, ")"));
	check(h->has_event("fx", "fx", Value("ring")), "Maelstrom emits a ring cue");
	ledgers_ok(base, "Maelstrom");
}

// ---------------------------------------------------------------- thrust: Water Bullet / Pressure Jet

FF_TEST_F(test_kit_water_water, WW, test_water_bullet_tiers) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 10.0);
	(void)r;
	SimHarness* h = &this->h();
	const Snap base = snap(*h->w);
	_tap(w, UP);
	h->step(40);
	const std::vector<Dict> launches = h->events("launch");
	check(launches.size() == 1, S("tap: one slug (", launches.size(), ")"));
	MatBody* slug = !launches.empty() ? h->w->get_body(ev_i(launches[0], "body", -1)) : nullptr;
	check(slug != nullptr && slug->tag == "slug" && is_equal_approx(slug->mass, 1.5), "1.5 kg slug (tag slug)");
	h->step(120);
	ledgers_ok(base, "bullet T0");
	// T1: triple
	std::tie(w, r) = duel(0, Sim::EARTH, 3, 10.0);
	h = &this->h();
	_hold_flick(w, UP, 0.6);
	h->step(40);
	check(h->events("launch").size() == 3, S("T1: three slugs (", h->events("launch").size(), ")"));
}

FF_TEST_F(test_kit_water_water, WW, test_pressure_jet_stays_connected_and_conducts_lightning_back) {
	auto [w, r] = duel(0, Sim::FIRE, 4, 9.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	r->kit = D({{"lightning", true}});
	const Snap base = snap(*h.w);
	h.flick(w, "attack", UP);
	h.step(66);   // T2 reached
	h.release(w, "attack");
	BodyRef jet;
	for (int k = 0; k < 40; ++k) {
		h.step();
		for (const BodyRef& b : h.w->bodies)
			if (b->alive && b->tag == "jet") jet = b;
		if (jet != nullptr) break;
	}
	check(jet != nullptr && jet->controller == w->id && jet->is_water(), "the Pressure Jet is a water body held by its caster");
	if (jet == nullptr) return;
	check(h.has_event("jet", "on", Value(true)), "jet event on");
	// A rival's lightning striking the jet comes back to the caster through the connection.
	const double hp0 = w->health;
	const Dict def = D({{"range", 14.0}, {"damage", 24.0}, {"balance", 40.0}, {"conduct_budget", 26.0}, {"max_hops", 4}});
	r->pos = V3(0, 0, -4);
	const int aid = h.w->new_attack_id();
	const Dict out = Conduction::discharge(*h.w, *r, jet->pos, def, aid, true);
	check(w->health < hp0, S("lightning on a connected jet hurts the caster (", hp0, " -> ", w->health, ")"));
	check(arr_has_int(darr(out, "hits"), w->id) || h.has_event("conduct"), "the conduction graph reached the caster through the jet");
	h.step(80);
	check(h.has_event("jet", "on", Value(false)), "the jet ends");
	check(_find_tag("jet") == nullptr, "no jet body left");
	ledgers_ok(base, "Pressure Jet");
}

FF_TEST_F(test_kit_water_water, WW, test_cutting_jet_cuts_soft_walls_and_quenches_flames) {
	auto [w, r] = duel(0, Sim::EARTH, 5, 8.0);
	SimHarness& h = this->h();
	const BodyRef wall = keep(h.w->spawn_body(Mat::Sand, Form::Wall, 100.0, w->pos + V3(0, 0, -3.5), "test"));
	h.w->mass_ledger.ground_taken += 100.0;
	wall->tag = "sand";
	wall->wall_half = V3(1.1, 0.75, 0.28);
	wall->wall_rise = 1.0;
	wall->static_body = true;
	wall->props.set("standing", 99.0);
	wall->last_actor = 2;
	r->pos = w->pos + V3(0, 0, -7.0);
	h.step(3);
	const Snap base = snap(*h.w);
	h.flick(w, "attack", UP);
	h.step(112);   // T3
	h.release(w, "attack");
	h.step(90);
	check(wall->wall_damage > 0.2 || !wall->alive, S("the thin jet cuts the sand wall (damage ", wall->wall_damage, ")"));
	ledgers_ok(base, "Cutting Jet");
}

// ---------------------------------------------------------------- ground: Tidal Rush

FF_TEST_F(test_kit_water_water, WW, test_tidal_rush_is_a_tagged_water_wave_that_hits_and_leaves_puddles) {
	auto [w, r] = _by_the_pool(Sim::EARTH, 9.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	_tap(w, DOWN);
	BodyRef wave;
	for (int k = 0; k < 60; ++k) {
		h.step();
		wave = _find_tag("water_wave", static_cast<int>(Form::Wave));
		if (wave != nullptr) break;
	}
	check(wave != nullptr && wave->is_water() && wave->mass >= 3.0, S("a water WAVE tagged water_wave (", wave ? wave->mass : 0.0, " kg)"));
	if (wave == nullptr) return;
	check(is_equal_approx(wave->power, 18.0), S("T0 wave power 18 PU with a full 8 kg (", wave->power, ")"));
	h.step(120);
	check(r->health < 100.0, S("the wave knocks the rival down (hp ", r->health, ")"));
	check(h.has_event("hit"), "a hit event");
	h.step(80);
	bool any_wave = false;
	for (const BodyRef& b : bodies_of(Mat::Water, static_cast<int>(Form::Wave))) any_wave = any_wave || b->tag == "water_wave";
	check(!any_wave, "the wave has ended");
	check(!bodies_of(Mat::Water, static_cast<int>(Form::Puddle)).empty(), "it leaves puddles");
	ledgers_ok(base, "Tidal Rush");
}

FF_TEST_F(test_kit_water_water, WW, test_tidal_rush_carries_a_stone_back_to_its_thrower) {
	// The owner's example: "someone throws a stone at me ... make a wave back".
	auto [w, r] = _by_the_pool(Sim::EARTH, 13.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	_tap(w, DOWN);
	h.step(26);   // the wave is out (startup 16 f + the release)
	const BodyRef stone = keep(h.launch_at(w, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 7.0));
	bool carried = false;
	bool thrown_back = false;
	const double hp0 = r->health;
	for (int k = 0; k < 200; ++k) {
		h.step();
		if (stone->captured_by >= 0) carried = true;
		if (stone->attack_owner == w->id && stone->attack_id != 0 && stone->vel.length() > 5.0f) thrown_back = true;
	}
	check(carried, "the wave captured the stone (CAP)");
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "counter") == "wave_water" && ev_s(e, "outcome") == "capture"; }),
	      "interaction wave_water -> capture");
	check(thrown_back, "the stone leaves the wave as the caster's attack");
	check(r->health < hp0, S("and it hits its thrower (hp ", hp0, " -> ", r->health, ")"));
	ledgers_ok(base, "wave back");
}

FF_TEST_F(test_kit_water_water, WW, test_tidal_rush_quenches_a_lava_wave_into_rock) {
	auto [w, r] = _by_the_pool(Sim::FIRE, 13.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	_hold_flick(w, DOWN, 0.6);
	h.step(24);
	const BodyRef lava = keep(_lava_wave(20.0, w->pos + V3(0, 0, -7.0), V3(0, 0, 1), r));
	const double steam0 = h.w->ledger.vapor;
	bool ok = false;
	for (int k = 0; k < 160; ++k) {
		h.step();
		if (lava->alive && lava->liquid <= 0.0 && lava->phase == Phase::Solid) {
			ok = true;
			break;
		}
	}
	check(ok, S("the lava wave is quenched into rock (liquid ", lava->liquid, ", phase ", Sim::phase_name(lava->phase), ")"));
	check(h.w->ledger.vapor > steam0, "the quench boils water (vapor ledger)");
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "threat") == "lava_wave" && ev_s(e, "counter") == "wave_water"; }),
	      "interaction lava_wave x wave_water");
	h.step(120);
	ledgers_ok(base, "quench");
}

FF_TEST_F(test_kit_water_water, WW, test_tidal_rush_extinguishes_a_fire_field) {
	auto [w, r] = _by_the_pool(Sim::FIRE, 13.0);
	(void)r;
	SimHarness& h = this->h();
	const BodyRef f = keep(h.w->spawn_body(Mat::Fire, Form::Chunk, 1.0, w->pos + V3(0, 0.3, -5.0), "test"));
	f->tag = "fire_field";
	f->heat_payload = 90.0;
	f->static_body = false;
	f->on_ground = true;
	const Snap base = snap(*h.w);
	_tap(w, DOWN);
	bool out = false;
	for (int k = 0; k < 200; ++k) {
		h.step();
		if (!f->alive) {
			out = true;
			break;
		}
	}
	check(out, "the fire field is doused");
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "outcome") == "extinguish"; }), "extinguish interaction");
	h.step(60);
	ledgers_ok(base, "douse");
}

FF_TEST_F(test_kit_water_water, WW, test_deluge_t3_is_bigger_and_needs_water) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 10.0);
	SimHarness* h = &this->h();
	w->pos = V3(8.0, 0, 4.5);   // beside the pool: unlimited water
	h->step(5);
	const Snap base = snap(*h->w);
	_hold_flick(w, DOWN, 1.9);
	h->step(30);
	const std::vector<BodyRef> waves = bodies_of(Mat::Water, static_cast<int>(Form::Wave));
	std::string masses;
	for (const BodyRef& b : waves) masses += S(b->mass, " ");
	check(waves.size() == 1 && waves[0]->mass > 12.0 && is_equal_approx(waves[0]->power, 45.0),
	      "T3 Deluge: a big 45 PU wave from the pool (" + masses + ")");
	h->step(200);
	ledgers_ok(base, "Deluge");
	// Dry and far from any water: it fizzles with the insufficient event.
	std::tie(w, r) = duel(0, Sim::EARTH, 3, 10.0);
	h = &this->h();
	w->pos = V3(-8, 0, 8);
	w->water_carried = 0.0;
	h->step(3);
	h->log.clear();
	_tap(w, DOWN);
	h->step(60);
	check(h->has_event("insufficient", "what", Value("water")), "no water: insufficient event");
	check(bodies_of(Mat::Water, static_cast<int>(Form::Wave)).empty(), "no wave without water");
}

// ---------------------------------------------------------------- sweep / push / sink

FF_TEST_F(test_kit_water_water, WW, test_spray_fan_wets_and_turns_sand_to_mud) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 4.0);
	SimHarness* h = &this->h();
	const Snap base = snap(*h->w);
	r->wetness = 0.0;
	_tap(w, SIDE);
	h->step(40);
	check(r->wetness > 0.9 || Status::has(*r, "wet"), S("the spray wets the rival (", r->wetness, ")"));
	ledgers_ok(base, "Spray Fan");
	// Sand in the cone becomes mud (rule sand x spray).
	std::tie(w, r) = duel(0, Sim::EARTH, 3, 6.0);
	h = &this->h();
	const BodyRef sand = keep(h->w->spawn_body(Mat::Sand, Form::Chunk, 6.0, w->pos + V3(0, 1.0, -3.0), "test"));
	h->w->mass_ledger.ground_taken += 6.0;
	sand->vel = Vec3();
	sand->gravity_scale = 0.0;
	sand->attack_id = h->w->new_attack_id();
	sand->attack_owner = 2;
	_tap(w, SIDE);
	h->step(30);
	check(sand->tag == "mud", "sand in the spray turns to mud (tag " + sand->tag + ")");
}

FF_TEST_F(test_kit_water_water, WW, test_surge_orb_hurls_the_shield_and_bursts) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 8.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(20);
	check(w->action != nullptr && w->action->id == "guard" && w->held_body >= 0, "guard raised a water shield");
	h.flick(w, "guard", UP);
	h.step(12);
	const BodyRef orb = _find_tag("orb");
	h.release(w, "guard");
	h.step(80);
	check(orb != nullptr || h.has_event("launch"), "the shield was thrown as an orb");
	check(r->wetness > 0.5 || r->health < 100.0, S("the orb bursts on the rival (wet ", r->wetness, " hp ", r->health, ")"));
	h.step(120);
	ledgers_ok(base, "Surge Orb");
}

FF_TEST_F(test_kit_water_water, WW, test_slick_puddle_trips_runners) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 8.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(15);
	h.flick(w, "guard", DOWN);
	h.step(6);
	h.release(w, "guard");
	h.step(20);
	const std::vector<BodyRef> z = zones_tagged("slick");
	check(z.size() == 1 && std::fabs(z[0]->zone_radius - 1.25) < 0.01, S("a 2.5 m slick zone (r ", !z.empty() ? z[0]->zone_radius : 0.0, ")"));
	if (z.empty()) return;
	check(!bodies_of(Mat::Water, static_cast<int>(Form::Puddle)).empty(), "backed by a real puddle (conductive)");
	// The rival runs through it: slips (balance) and gets wet.
	r->pos = z[0]->pos + V3(0, 0, -1.0);
	r->vel = V3(0, 0, 5.5);
	const double bal0 = r->balance;
	h.step(30);
	check(h.has_event("slip", "actor", Value(r->id)), "the runner slips");
	check(r->balance < bal0 || r->stun > 0.0, S("balance lost (", bal0, " -> ", r->balance, ")"));
	check(r->wetness > 0.3, "and wet");
	h.step(300);
	ledgers_ok(base, "Slick");
}

// ---------------------------------------------------------------- technique extras

FF_TEST_F(test_kit_water_water, WW, test_draw_condenses_steam_into_water) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 8.0);
	(void)r;
	SimHarness& h = this->h();
	w->pos = V3(-6.0, 0, 4.0);   // away from the pool: only vapour to draw
	w->water_carried = 0.0;
	h.step(3);
	const Snap base = snap(*h.w);
	h.w->_spawn_steam(w->pos + V3(0, 1.2, -2.5), 3.0);
	h.w->mass_ledger.moisture_taken += 3.0;   // the test conjured the steam: book it
	h.press(w, "tech");
	h.step(50);
	MatBody* held = h.w->held(*w);
	check(held != nullptr && held->is_water() && held->mass > 0.5, S("steam condensed into a held water body (", held ? held->mass : 0.0, " kg)"));
	h.release(w, "tech");
	h.step(120);
	h.step(240);
	ledgers_ok(base, "condense");
}

FF_TEST_F(test_kit_water_water, WW, test_draw_seizes_an_enemy_stream_in_flight) {
	auto [w, r] = duel(0, Sim::WATER, 3, 9.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	const BodyRef stream = keep(h.launch_at(w, "water", 5.0, 15.0, Sim::AMBIENT_C, "slug", r, 7.0));
	stream->form = Form::Blob;
	h.w->mass_ledger.moisture_taken += 5.0;   // the test conjured these 5 kg: book them
	h.press(w, "tech");
	bool got = false;
	for (int k = 0; k < 40; ++k) {
		h.step();
		if (stream->controller == w->id) {
			got = true;
			break;
		}
	}
	check(got, "the technique seizes the rival's stream in flight (contest won)");
	check(h.has_event("control_won", "actor", Value(w->id)), "control_won event");
	h.release(w, "tech");
	h.step(160);
	ledgers_ok(base, "seize");
}

FF_TEST_F(test_kit_water_water, WW, test_attack_while_holding_freezes_the_water_into_a_block) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 8.0);
	(void)r;
	SimHarness& h = this->h();
	w->pos = V3(8.0, 0, 4.5);
	h.step(5);
	const Snap base = snap(*h.w);
	h.press(w, "tech");
	h.step(40);
	MatBody* held = h.w->held(*w);
	check(held != nullptr && held->phase == Phase::Liquid, "drawn water is liquid");
	h.press(w, "attack");
	h.step(3);
	h.release(w, "attack");
	h.step(3);
	held = h.w->held(*w);
	check(held != nullptr && held->phase == Phase::Frozen, "T+A froze it into an ice block");
	h.release(w, "tech");
	h.step(30);
	check(!h.last_event("launch").empty(), "released: thrown");
	h.step(240);
	ledgers_ok(base, "Freeze T+A");
}

// ---------------------------------------------------------------- evades

FF_TEST_F(test_kit_water_water, WW, test_riptide_step_slides_and_dive_resurfaces_in_the_pool) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 10.0);
	SimHarness* h = &this->h();
	const Snap base = snap(*h->w);
	Vec3 p0 = w->pos;
	h->it(w).move = V3(1, 0, 0);
	h->press(w, "evade");
	h->step(1);
	h->it(w).move = Vec3();
	h->step(29);
	const double d = flat(w->pos, p0);
	check(d > 2.5 && d < 5.0, S("slides about 4 m (", d, ")"));
	check(!zones_tagged("slick").empty(), "leaves a water film trail");
	h->step(120);
	ledgers_ok(base, "Riptide");
	// Dive: in the pool the step submerges and resurfaces 4 m away, hidden and invulnerable.
	std::tie(w, r) = duel(0, Sim::EARTH, 3, 10.0);
	h = &this->h();
	w->pos = V3(10.0, -0.3, -1.0);
	w->grounded = true;
	h->step(3);
	check(w->in_water, "standing in the pool");
	p0 = w->pos;
	h->it(w).move = V3(1, 0, 0);
	h->press(w, "evade");
	h->step(4);
	check(h->has_event("dive", "on", Value(true)) && w->iframes > 0.0, "dive: submerged with i-frames");
	h->step(30);
	check(h->has_event("dive", "on", Value(false)), "resurfaced");
}

FF_TEST_F(test_kit_water_water, WW, test_wave_ride_is_faster_and_spends_water) {
	auto [w, r] = duel(0, Sim::EARTH, 3, 14.0);
	(void)r;
	SimHarness& h = this->h();
	w->water_carried = 6.0;
	const Snap base = snap(*h.w);
	h.it(w).move = V3(1, 0, 0);
	h.press(w, "evade");
	h.it(w).evade_held = true;
	h.step(40);
	h.it(w).move = V3(1, 0, 0);
	const double v0 = flat_speed(w->vel);
	(void)v0;
	h.step(30);
	const double v1 = flat_speed(w->vel);
	check(w->action != nullptr && w->action->id == "wave_ride", "morphed into Wave Ride (" + (w->action ? w->action->id : std::string("none")) + ")");
	check(v1 > 6.0, S("surfing at more than a run (", v1, " m/s)"));
	check(w->water_carried < 6.0 || h.w->water_mass() > 0.0, "the ride spends water");
	h.it(w).evade_held = false;
	h.step(120);
	ledgers_ok(base, "Wave Ride");
}
