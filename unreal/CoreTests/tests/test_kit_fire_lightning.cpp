// Port of game/tests/sim/test_kit_fire_lightning.gd: Fire / Lightning (sub 2): "lightning blasts through stone" (Storm Bolt
// vs a Bulwark arrives with E 21 while the T1 Bolt is grounded), Skybreak from above, Static Ward absorb / return,
// Conductor's Hand relays around cover, Rail Arc follows a held water jet into its holder, Ground Current lives on
// conductive ground only.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct LightningFx : FireCase {
	void _hold_strike(ActorState* p, int ticks) {
		h().press(p, "attack");
		h().step(ticks);
		h().release(p, "attack");
		h().step(2);
	}
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
	void puddle_trail(ActorState* p, bool frozen) {
		for (int k = 0; k < 4; ++k) {
			MatBody* pd = h().w->spawn_body(Mat::Water, Form::Puddle, 6.0, V3(0, 0, p->pos.z - 1.6 - 1.9 * k), "test");
			pd->update_radius_puddle();
			pd->radius = 1.1;
			if (frozen && k == 1) {
				pd->liquid = 0.0;
				pd->phase = Phase::Frozen;
			}
		}
	}
};
using EU::keep;
}  // namespace

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_storm_bolt_blasts_through_a_bulwark_and_the_bolt_is_grounded) {
	{
		// Bolt T1 (E 24 <= wall CP 30): grounded, the rival behind is safe.
		auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		BodyRef wall = keep(bulwark(r, V3(0, 0, p->pos.z - 3.0), p->facing));
		_hold_strike(p, 42);
		const Dict ev = h.last_event("lightning");
		check(!ev.empty() && dbool(ev, "blocked"), "Bolt T1 is grounded by the wall");
		check(r->health == 100.0 && wall->alive, "rival safe, wall standing");
	}
	// Storm Bolt T2 (E 36 > 30): the wall shatters, the bolt continues with 36 - 0.5 x 30 = 21.
	auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef wall = keep(bulwark(r, V3(0, 0, p->pos.z - 3.0), p->facing));
	const Snap base = snap(*h.w);
	_hold_strike(p, 76);
	const Dict ev = h.last_event("lightning");
	check(!ev.empty() && !dbool(ev, "blocked") && arr_has_int(darr(ev, "hits"), r->id), "the Storm Bolt reaches the rival");
	near(dnum(ev, "e", 0.0), 21.0, 1e-6, "it arrives with E 21");
	check(!wall->alive && h.has_event("wall_crumble"), "the wall shattered");
	near(100.0 - r->health, 30.0 * 21.0 / 36.0, 0.01, "damage scaled by what is left of the bolt");
	h.step(30);
	ledgers_ok(base, "storm bolt");
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_skybreak_strikes_from_above_over_a_wall_and_deafens) {
	auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	bulwark(r, V3(0, 0, p->pos.z - 3.0), p->facing);
	_hold_strike(p, 112);
	check(h.any_event("telegraph", [](const Dict& e) { return dstr(e, "move", "") == "skybreak"; }), "a 0.4 s telegraph at the target");
	check(r->health == 100.0, "nothing yet");
	h.step(26);
	check(r->health < 100.0 - 30.0, S("the strike from above hits behind the wall (", r->health, ")"));
	ActorState* rr = r;
	check(r->status.has("deafened") ||
	          h.any_event("status", [&](const Dict& e) { return is(e, "actor", rr->id) && dstr(e, "status") == "deafened"; }),
	      "thunder deafens");
	check(h.has_event("thunder"), "thunder event");
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_static_ward_stores_the_bolt_and_static_burst_returns_it) {
	// The dedicated electric answer: a T0 ward (CP 12) drinks up to 2.5 x 12 = 30 of a bolt, so a T1 Bolt (E 24) is stored whole.
	auto [p, r] = duel(2, Sim::FIRE, 5, 3.5);
	SimHarness& h = this->h();
	r->subs[Sim::FIRE] = 2;
	h.press(p, "guard");
	h.step(20);   // not perfect
	_hold_strike(r, 42);   // the rival's Bolt (E 24)
	check(h.has_event("static_absorb"), "the ward stores the bolt");
	near(p->static_charge, 24.0, 1e-6, "the whole bolt stored as static");
	near(100.0 - p->health, 0.0, 0.01, "nothing taken");
	check(p->guarding, "the ward stays up");
	h.release(p, "guard");
	ActorState* pp = p;
	h.until([&]() { return pp->stun <= 0.0; }, 60);
	h.until([&]() { return pp->action == nullptr; }, 40);   // the guard's recovery ends
	h.press(p, "guard");
	h.step(6);
	h.flick(p, "guard", static_cast<int>(Gesture::Up));
	h.step(30);
	const Dict sb = h.last_event("static_burst");
	check(!sb.empty() && absf(dnum(sb, "e") - 24.0) < 1e-6, S("Static Burst releases it (", sb.get("e"), ")"));
	check(r->health < 100.0 && p->static_charge == 0.0, S("back at the rival (", r->health, ")"));
	h.release(p, "guard");
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_perfect_static_ward_absorbs_fully_or_returns_with_redirect_current) {
	for (bool redirect : {false, true}) {
		auto [p, r] = duel(2, Sim::FIRE, 5, 8.0, redirect ? D({{"redirect_current", true}}) : Dict());
		SimHarness& h = this->h();
		r->subs[Sim::FIRE] = 2;
		h.press(r, "attack");
		h.step(40);
		h.press(p, "guard");
		h.step(1);
		h.release(r, "attack");
		h.step(3);
		check(p->health == 100.0, S("redirect=", redirect, ": perfect ward, no damage (", p->health, ")"));
		if (redirect) {
			check(h.has_event("lightning_redirect") && r->health < 100.0, S("Return Current sends it back (rival ", r->health, ")"));
			near(100.0 - r->health, 24.0 * 0.8, 0.01, "at 80 %");
		} else {
			near(p->static_charge, 24.0, 1e-6, "fully absorbed into static");
		}
		h.release(p, "guard");
		h.step(20);
	}
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_conductors_hand_relays_a_bolt_around_cover) {
	SimHarness& h = H(6);
	ActorState* p = h.actor("F", V3(-3.75, 0, 5.0), 0, Dict(), Sim::FIRE);
	ActorState* r = h.actor("R", V3(-3.75, 0, -4.0), 1, Dict(), Sim::EARTH);
	r->is_dummy = true;
	p->subs[Sim::FIRE] = 2;
	h.step(20);
	// A direct Bolt is stopped by the cover wall.
	_hold_strike(p, 42);
	check(dbool(h.last_event("lightning"), "blocked", false) && r->health == 100.0, "the cover wall stops the direct bolt");
	h.step(60);
	MatBody* rod = h.w->spawn_body(Mat::Metal, Form::Chunk, 3.0, V3(-0.2, 0.25, -1.0), "test");
	rod->tag = "rod";
	rod->on_ground = true;
	rod->static_body = true;
	h.log.clear();
	Vec3 dir = rod->pos - p->pos;
	dir.y = 0.0f;
	h.aim(p, dir);
	h.press(p, "tech");
	h.step(30);
	check(rod->charge > 0.0, S("Conductor's Hand charges the rod (", rod->charge, ")"));
	h.release(p, "tech");
	h.step(4);
	const Dict al = h.last_event("arc_link");
	check(!al.empty() && arr_has_int(darr(al, "hits"), r->id), S("Arc Link banks the bolt through the rod into the rival (", al, ")"));
	check(r->health < 100.0, S("behind cover (", r->health, ")"));
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_rail_arc_follows_a_held_water_jet_into_its_holder) {
	SimHarness& h = H(6);
	ActorState* p = h.actor("F", V3(0, 0, 6.0), 0, Dict(), Sim::FIRE);
	ActorState* r = h.actor("R", V3(3.5, 0, -3.0), 1, Dict(), Sim::WATER);
	r->is_dummy = true;
	p->subs[Sim::FIRE] = 2;
	h.step(20);
	MatBody* jet = h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(1.6, 1.25, -1.5), "test");
	jet->tag = "jet";
	h.w->take_control(*r, *jet, 0.97, "jet");
	jet->hold_point = jet->pos;
	jet->radius = 1.8;
	h.aim(p, V3(0, 0, -1));
	h.flick(p, "attack", static_cast<int>(Gesture::Up));
	h.step(3);
	h.release(p, "attack");
	h.until([&]() { return h.has_event("lightning"); }, 40);
	const Dict ev = h.last_event("lightning");
	check(dbool(ev, "rail", false), "a rail arc");
	check(r->health < 100.0 && arr_has_int(darr(ev, "hits"), r->id), S("it follows the jet into its holder (", r->health, ")"));
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_ground_current_needs_conductive_ground) {
	{
		// Dry stone: it dies after about 2 m and never reaches a rival 8 m away.
		auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		h.flick(p, "attack", static_cast<int>(Gesture::Down));
		h.step(3);
		h.release(p, "attack");
		h.step(60);
		check(h.events("current_grounded").size() >= 1 && r->health == 100.0, "on dry stone it grounds out");
	}
	// A trail of puddles to the rival: it races along and shocks them.
	for (bool frozen : {false, true}) {
		auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
		SimHarness& h = this->h();
		r->is_dummy = true;
		puddle_trail(p, frozen);
		h.flick(p, "attack", static_cast<int>(Gesture::Down));
		h.step(3);
		h.release(p, "attack");
		h.step(70);
		if (frozen) check(h.has_event("insulated") && r->health == 100.0, "a frozen puddle stops the current");
		else check(r->health < 100.0, S("along wet ground it reaches the rival (", r->health, ")"));
	}
	// Grounding makes the rival immune.
	auto [p, r] = duel(2, Sim::EARTH, 5, 8.0);
	SimHarness& h = this->h();
	puddle_trail(p, false);
	Status::apply(*h.w, *r, "grounding", 3.0, 1.0, r->id);
	h.flick(p, "attack", static_cast<int>(Gesture::Down));
	h.step(3);
	h.release(p, "attack");
	h.step(70);
	check(r->health == 100.0, S("Grounding is immune (", r->health, ")"));
}

FF_TEST_F(test_kit_fire_lightning, LightningFx, test_arc_fan_forks_into_two_rivals) {
	SimHarness& h = H(6);
	ActorState* p = h.actor("F", V3(0, 0, 4.0), 0, Dict(), Sim::FIRE);
	ActorState* r1 = h.actor("R1", V3(-2.2, 0, -0.5), 1, Dict(), Sim::EARTH);
	ActorState* r2 = h.actor("R2", V3(2.2, 0, -0.5), 1, Dict(), Sim::EARTH);
	r1->is_dummy = true;
	r2->is_dummy = true;
	p->subs[Sim::FIRE] = 2;
	h.step(20);
	h.aim(p, V3(0, 0, -1));
	h.flick(p, "attack", static_cast<int>(Gesture::Side));
	h.step(3);
	h.release(p, "attack");
	h.step(30);
	check(r1->health < 100.0 && r2->health < 100.0, S("both rivals forked (", r1->health, ", ", r2->health, ")"));
}
