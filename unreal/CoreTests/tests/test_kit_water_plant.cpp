// Port of game/tests/sim/test_kit_water_plant.gd: Water / Plant (sub 3, docs/MOVESET.md §7.8, P2): vines grow from water
// (booked water_to_plant), burn x3, are brittle when frozen. Bramble Lash, Burr Shot, Root Snare, Thicket Fan, Living
// Lattice (+ Catch & Sling), Lattice Roll, Deep Roots, Vinegrip (+ Wrap, Hook), Vine Swing, Canopy.
#include "ff_test.h"
#include "kit_water_util.h"
#include "sim_harness.h"

#include "Sim/Materials.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
constexpr int NONE = static_cast<int>(Gesture::None);
constexpr int UP = static_cast<int>(Gesture::Up);
constexpr int DOWN = static_cast<int>(Gesture::Down);
constexpr int SIDE = static_cast<int>(Gesture::Side);

bool is_eq(const Dict& e, const char* k, const char* v) { return ev_s(e, k) == v; }

struct WP : WaterCase {
	std::pair<ActorState*, ActorState*> _plant_duel(double dist = 8.0, int rival_element = Sim::EARTH) {
		return duel(3, rival_element, 3, dist);
	}
	void _tap(ActorState* p, int gesture, int hold_ticks = 2) {
		h().flick(p, "attack", gesture);
		h().step(hold_ticks);
		h().release(p, "attack");
	}
	void _hold(ActorState* p, int gesture, double secs) {
		h().flick(p, "attack", gesture);
		h().step(static_cast<int>(secs * 60.0));
		h().release(p, "attack");
	}
	BodyRef _raise(ActorState* w, int ticks = 18) {
		h().press(w, "guard");
		h().step(ticks);
		return keep(h().w->get_body(w->wall_body));
	}
	static double dist(const ActorState* a, const ActorState* b) { return static_cast<double>(a->pos.distance_to(b->pos)); }
	static IxCtx site(const std::string& s) {
		IxCtx c;
		c.site = s;
		return c;
	}
	bool rooted(ActorState* r) { return Status::rooted(*r) || h().has_event("status", "status", Value("rooted")); }
};
}  // namespace

FF_TEST_F(test_kit_water_plant, WP, test_bramble_lash_yanks_a_target_in_and_the_vine_withers) {
	auto [w, r] = _plant_duel(4.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	const Snap base = snap(*h->w);
	const double d0 = dist(w, r);
	const double skin0 = w->water_carried;
	_tap(w, NONE);
	h->step(40);
	check(h->has_event("action", "move", Value("bramble_lash")), "Bramble Lash ran");
	check(r->health < 100.0 || h->has_event("hit"), "the whip hits");
	check(dist(w, r) < d0 - 0.4, S("the target is yanked toward the caster (", d0, " -> ", dist(w, r), " m)"));
	near(skin0 - w->water_carried, 1.0, 0.01, "1 kg of water went into the vine");
	check(h->w->mass_ledger.water_to_plant >= 1.0, "booked water_to_plant");
	check(bodies_of(Mat::Plant).size() == 1, "one vine lies where the whip ended");
	h->step(240);
	check(bodies_of(Mat::Plant).empty(), "and withers away");
	ledgers_ok(base, "Bramble Lash");
	// T3 Briar Storm: 360 degrees
	std::tie(w, r) = _plant_duel(4.0);
	h = &this->h();
	r->pos = w->pos + V3(0, 0, 3.0);   // behind
	r->is_dummy = true;
	_hold(w, NONE, 1.9);
	h->step(30);
	check(r->health < 100.0, S("Briar Storm hits a rival behind the caster (hp ", r->health, ")"));
}

FF_TEST_F(test_kit_water_plant, WP, test_burr_shot_seeds_sprout_snares_that_root) {
	auto [w, r] = _plant_duel(8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	_tap(w, UP);
	h.step(10);
	check(h.events("launch").size() == 3, S("three burrs (", h.events("launch").size(), ")"));
	bool was_rooted = false;
	for (int k = 0; k < 100; ++k) {
		h.step();
		if (Status::rooted(*r)) was_rooted = true;
	}
	check(was_rooted || h.has_event("status", "status", Value("rooted")), "a burr sprouted under the rival and rooted them");
	check(!zones_tagged("snare").empty(), "snare zones stand where the burrs landed");
	h.step(400);
	check(zones_tagged("snare").empty(), "they wither");
	ledgers_ok(base, "Burr Shot");
}

FF_TEST_F(test_kit_water_plant, WP, test_root_snare_travels_underground_and_roots_but_not_fliers) {
	auto [w, r] = _plant_duel(9.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	const Snap base = snap(*h->w);
	_tap(w, DOWN);
	BodyRef wave;
	for (int k = 0; k < 60; ++k) {
		h->step();
		for (const BodyRef& b : h->w->bodies)
			if (b->alive && b->tag == "roots") wave = b;
		if (wave != nullptr) break;
	}
	check(wave != nullptr && wave->mat == Mat::Plant && wave->form == Form::Wave, "a PLANT wave tagged roots");
	h->step(60);
	check(rooted(r), "the roots erupt under the target: rooted");
	h->step(300);
	ledgers_ok(base, "Root Snare");
	// A flying (levitating) fighter passes over the line.
	std::tie(w, r) = _plant_duel(9.0);
	h = &this->h();
	r->is_dummy = true;
	r->flying = true;
	_tap(w, DOWN);
	h->step(120);
	check(!Status::rooted(*r), "a fighter in the air is not rooted");
}

FF_TEST_F(test_kit_water_plant, WP, test_thicket_fan_slows_and_catches) {
	auto [w, r] = _plant_duel(5.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	_tap(w, SIDE);
	h.step(30);
	const std::vector<BodyRef> z = zones_tagged("briar");
	check(z.size() == 1 && z[0]->mat == Mat::Plant && std::fabs(z[0]->zone_radius - 2.4) < 0.01, S("a briar zone of vine (", z.size(), ")"));
	if (z.empty()) return;
	check(Interactions::counter_class(*z[0], h.w) == "briar", "counter class briar");
	r->pos = z[0]->pos;
	h.step(10);
	check(Status::has(*r, "slowed"), "whoever stands in it is slowed (-40%)");
	const BodyRef stone = keep(h.launch_at(w, "stone", 10.0, 14.0, Sim::AMBIENT_C, "", r, 3.5));
	const double v0 = static_cast<double>(stone->vel.length());
	h.step(12);
	const double v1 = static_cast<double>(stone->vel.length());
	check(v1 < v0 * 0.9 || stone->attack_id == 0, S("a small projectile is caught and slowed (", v0, " -> ", v1, ")"));
	h.step(400);
	ledgers_ok(base, "Thicket Fan");
}

FF_TEST_F(test_kit_water_plant, WP, test_living_lattice_captures_catches_and_slings_back) {
	auto [w, r] = _plant_duel(9.0);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	Snap base = snap(*h->w);
	BodyRef wall = _raise(w);
	check(wall != nullptr && wall->mat == Mat::Plant && wall->tag == "vine" && wall->form == Form::Wall,
	      "a vine WALL (" + (wall ? wall->describe() : std::string("none")) + ")");
	if (wall == nullptr) return;
	near(wall->mass * Materials::hardness(*wall), 16.0, 0.01, "40 kg x 0.4 = CP 16");
	check(h->w->mass_ledger.water_to_plant >= 40.0 - 1e-6, "its 40 kg are booked water -> vine");
	// A stone is captured (x1.3).
	const BodyRef stone = keep(h->launch_at(w, "stone", 20.0, 17.0, Sim::AMBIENT_C, "", r, 6.0));
	h->step(40);
	check(stone->captured_by == wall->id ||
	          h->any_event("interaction", [](const Dict& e) { return is_eq(e, "counter", "wall_vine") && is_eq(e, "outcome", "capture"); }),
	      "the stone is caught in the lattice");
	check(w->health == 100.0, "and never reaches the fighter");
	h->release(w, "guard");
	h->step(120);
	ledgers_ok(base, "Living Lattice");
	// Catch & Sling: a perfect guard throws the projectile back at its thrower.
	std::tie(w, r) = _plant_duel(9.0);
	h = &this->h();
	r->is_dummy = true;
	base = snap(*h->w);
	wall = _raise(w);
	if (wall == nullptr) {
		check(false, "wall up");
		return;
	}
	const BodyRef st2 = keep(h->launch_at(w, "stone", 12.0, 17.0, Sim::AMBIENT_C, "", r, 6.0));
	AgentRef counter = Agent::of_body(*h->w, *wall);
	counter->actor = w;
	counter->perfect = true;
	AgentRef th = Agent::of_body(*h->w, *st2, w);
	const IxResult res = Interactions::resolve(*h->w, *th, *counter, site("wall"));
	check(res.outcome == "redirect" && res.rule_id.find("vine") != std::string::npos, "perfect: slung back (" + res.outcome + " via " + res.rule_id + ")");
	check(st2->attack_owner == w->id && st2->vel.dot(V3(0, 0, -1)) > 5.0f, "now flying at the thrower as the caster's attack");
	h->step(80);
	check(r->health < 100.0, S("and it hits them (hp ", r->health, ")"));
	h->release(w, "guard");
	h->step(200);
	ledgers_ok(base, "Catch & Sling");
}

FF_TEST_F(test_kit_water_plant, WP, test_a_flame_burns_the_lattice_x3_and_water_feeds_it) {
	auto [w, r] = _plant_duel(9.0, Sim::FIRE);
	SimHarness* h = &this->h();
	r->is_dummy = true;
	Snap base = snap(*h->w);
	BodyRef wall = _raise(w);
	check(wall != nullptr, "wall up");
	if (wall == nullptr) return;
	double m0 = wall->mass;
	const BodyRef f = keep(h->w->spawn_body(Mat::Fire, Form::Chunk, 1.0, wall->pos + V3(0, 1.0, -1.5), "test"));
	f->heat_payload = 160.0;
	h->w->ledger.generated += 160.0;
	f->vel = V3(0, 0, 8.0);
	f->gravity_scale = 0.0;
	f->attack_id = h->w->new_attack_id();
	f->attack_owner = r->id;
	f->hit_set.add(r->id);
	f->max_life = 3.0;
	h->step(40);
	check(wall->mass < m0 - 2.0 || !wall->alive, S("the flame burned vine away (", m0, " -> ", wall->alive ? wall->mass : 0.0, " kg)"));
	check(h->w->mass_ledger.burned > 2.0, S("booked as burned (", h->w->mass_ledger.burned, ")"));
	h->release(w, "guard");
	h->step(240);
	ledgers_ok(base, "burn");
	// Water feeds it: a water body hitting the lattice grows it.
	std::tie(w, r) = _plant_duel(9.0, Sim::WATER);
	h = &this->h();
	r->is_dummy = true;
	base = snap(*h->w);
	wall = _raise(w);
	if (wall == nullptr) {
		check(false, "wall up");
		return;
	}
	m0 = wall->mass;
	const BodyRef stream = keep(h->launch_at(w, "water", 6.0, 16.0, Sim::AMBIENT_C, "slug", r, 6.0));
	h->w->mass_ledger.moisture_taken += 6.0;
	stream->form = Form::Blob;
	h->step(40);
	check(wall->mass > m0 + 2.0, S("the vines drink the water and grow (", m0, " -> ", wall->mass, " kg)"));
	h->release(w, "guard");
	h->step(240);
	ledgers_ok(base, "feed");
}

FF_TEST_F(test_kit_water_plant, WP, test_frozen_vines_are_brittle) {
	auto [w, r] = _plant_duel(9.0);
	(void)r;
	SimHarness& h = this->h();
	_raise(w);
	const BodyRef thorns = keep(h.w->spawn_body(Mat::Plant, Form::Chunk, 4.0, w->pos + V3(0, 0.3, -3.0), "test"));
	h.w->mass_ledger.water_to_plant += 4.0;
	const Snap base = snap(*h.w);
	Agent fr;
	fr.kind = "volume";
	fr.ccls = "frost";
	fr.cls = "frost";
	fr.power = 12.0;
	fr.ch.set("C", 12.0);
	AgentRef th = Agent::of_body(*h.w, *thorns);
	const IxResult res = Interactions::resolve(*h.w, *th, fr);
	check(res.outcome == "transform" && res.to == "brittle" && thorns->hardness < 0.1, S("frost x vine -> brittle (hardness ", thorns->hardness, ")"));
	h.release(w, "guard");
	h.step(240);
	ledgers_ok(base, "brittle");
}

FF_TEST_F(test_kit_water_plant, WP, test_lattice_roll_entangles) {
	auto [w, r] = _plant_duel(7.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	const BodyRef wall = _raise(w);
	if (wall == nullptr) {
		check(false, "wall up");
		return;
	}
	h.flick(w, "guard", UP);
	h.step(6);
	h.release(w, "guard");
	const Vec3 p0 = wall->pos;
	h.step(80);
	const double moved = static_cast<double>(wall->pos.distance_to(p0));
	check(wall->alive && moved > 3.0, S("the lattice rolled forward (", moved, " m)"));
	check(Status::has(*r, "entangled") || h.has_event("status", "status", Value("entangled")), "and entangled the rival");
	h.step(400);
	ledgers_ok(base, "Lattice Roll");
}

FF_TEST_F(test_kit_water_plant, WP, test_deep_roots_anchor_beats_a_tornado_and_drinks_puddles) {
	auto [w, r] = _plant_duel(7.0);
	(void)r;
	SimHarness& h = this->h();
	Snap base = snap(*h.w);
	h.press(w, "guard");
	h.step(10);
	h.flick(w, "guard", DOWN);
	h.step(20);
	check(w->anchored && w->stance == "roots", "rooted: anchored (" + w->stance + ")");
	AgentRef anchor = Agent::of_stance(h.w, *w);
	check(anchor->power >= 35.0, S("anchor CP 35 (", anchor->power, ")"));
	AgentRef torn = threat(*h.w, "tornado", 30.0, 0.0, "P");
	const IxResult pr = Interactions::predict(h.w, *torn, *anchor);
	check(pr.band == "full", S("a 30 PU tornado cannot lift an anchored fighter (ratio ", pr.ratio, ")"));
	// drinks puddles under the fighter
	w->water_carried = 2.0;
	MatBody* pud = h.w->spawn_body(Mat::Water, Form::Stream, 3.0, w->pos + V3(0, 0.3, 0), "test");
	h.w->mass_ledger.moisture_taken += 3.0;
	h.w->_water_to_puddle(*pud);
	base = snap(*h.w);
	h.step(120);
	check(w->water_carried > 2.9, S("the puddle was drunk into the waterskin (", w->water_carried, " kg)"));
	h.release(w, "guard");
	h.step(40);
	check(!w->anchored, "released: free again");
	ledgers_ok(base, "Deep Roots");
}

FF_TEST_F(test_kit_water_plant, WP, test_vinegrip_wins_at_long_range_and_wraps) {
	auto [w, r] = _plant_duel(12.0, Sim::EARTH);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const Snap base = snap(*h.w);
	const BodyRef stone = keep(h.launch_at(w, "stone", 20.0, 6.0, Sim::AMBIENT_C, "", r, 8.5));
	stone->attack_owner = r->id;
	h.press(w, "tech");
	bool got = false;
	for (int k = 0; k < 40; ++k) {
		h.step();
		if (stone->controller == w->id) {
			got = true;
			break;
		}
	}
	check(got, "a stone 8.5 m away is seized (REC, 9 m reach)");
	check(h.has_event("control_won", "actor", Value(w->id)), "the grip contest was won");
	// Wrap: attack while holding -> the body roots whoever it hits.
	h.press(w, "attack");
	h.step(3);
	h.release(w, "attack");
	h.step(3);
	MatBody* held = h.w->held(*w);
	check(held != nullptr && dstr(held->props, "hit_status", "") == "rooted", "Wrap: the held body carries a rooting hit");
	h.release(w, "tech");
	h.step(160);
	check(rooted(r) || r->health < 100.0, "it roots the rival on hit");
	h.step(300);
	ledgers_ok(base, "Vinegrip");
}

FF_TEST_F(test_kit_water_plant, WP, test_vinegrip_hooks_a_fighter) {
	auto [w, r] = _plant_duel(7.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	const double d0 = dist(w, r);
	const double bal0 = r->balance;
	h.press(w, "tech");
	h.step(30);
	h.release(w, "tech");
	h.step(40);
	check(h.has_event("hook", "target", Value(r->id)), "Hook event");
	check(dist(w, r) < d0 - 1.0, S("the fighter is yanked toward the caster (", d0, " -> ", dist(w, r), " m)"));
	check(r->balance < bal0, S("-20 balance (", bal0, " -> ", r->balance, ")"));
}

FF_TEST_F(test_kit_water_plant, WP, test_vine_swing_to_a_pillar_and_canopy_hover) {
	// The pillar at (-13, 12.5..13.5) of the lab: swing toward it.
	{
		SimHarness& h = H(3);
		ActorState* w = h.actor("W", V3(-8.0, 0, 10.0), 0, Dict(), Sim::WATER);
		ActorState* r = h.actor("R", V3(6.0, 0, -4.0), 1, Dict(), Sim::EARTH);
		r->is_dummy = true;
		w->subs[Sim::WATER] = 3;
		h.step(5);
		const Snap base = snap(*h.w);
		const Vec3 p0 = w->pos;
		h.it(w).move = V3(-1, 0, 0.5).normalized();
		h.press(w, "evade");
		h.step(1);
		h.it(w).move = Vec3();
		h.step(30);
		check(h.has_event("swing", "anchored", Value(true)), "an anchor was found within 9 m");
		check(w->pos.distance_to(p0) > 3.0f, S("swung ", w->pos.distance_to(p0), " m"));
		h.step(80);
		ledgers_ok(base, "Vine Swing");
	}
	// Canopy: hold evade, hover about 1.5 s then drop.
	auto [w, r] = _plant_duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	h.it(w).move = Vec3();
	h.press(w, "evade");
	h.it(w).evade_held = true;
	h.step(30);
	check(w->action != nullptr && w->action->id == "canopy", "morphed into Canopy (" + (w->action ? w->action->id : std::string("none")) + ")");
	check(w->flying && w->pos.y > 0.4f, S("hovering (y ", w->pos.y, ")"));
	h.step(60);
	h.step(40);
	check(!w->flying, "let go after about 1.5 s");
	h.it(w).evade_held = false;
	h.step(60);
}
