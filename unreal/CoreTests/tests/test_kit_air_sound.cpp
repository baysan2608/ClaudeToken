// Port of game/tests/sim/test_kit_air_sound.gd: Air / Sound (sub 3): Clap -> Shout -> Roar -> Resonance (disrupt, shatter,
// deafen), Sound Lance bank shots, Tremor Hum, Echo Ring, Sound Barrier + Echo Return, Thunder Step, Ground Ping,
// Flight, Boom Step, Hover, the Sound column. Clip existence checks are not ported (clip table: animation stream).
#include "ff_test.h"
#include "kit_air_util.h"
#include "sim_harness.h"

#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
const char* const MOVES[] = {"sound_clap", "sound_lance", "sound_tremor", "sound_echo_ring", "sound_barrier", "sound_thunder_step", "sound_ping",
                             "sound_flight", "sound_boom_step", "sound_hover"};

struct AS : AirCase {
	std::pair<ActorState*, ActorState*> _duel(double dist = 10.0) { return duel(3, Sim::EARTH, 3, dist); }
	BodyRef _ice_shard(Vec3 pos, double mass = 4.0) {
		SimHarness& hh = h();
		MatBody* b = hh.w->spawn_body(Mat::Water, Form::Shard, mass, pos, "test");
		hh.w->mass_ledger.moisture_taken += mass;
		const double e0 = b->thermal_energy();
		b->liquid = 0.0;
		b->temp = -5.0;
		b->phase = Phase::Frozen;
		hh.w->ledger.freeze_dump += b->thermal_energy() - e0;
		b->gravity_scale = 0.0;
		return keep(b);
	}
	std::string predict(const AgentRef& th, const AgentRef& c) { return Interactions::predict(h().w, *th, *c).outcome; }
	bool ix(const char* threat_cls, const char* counter, const char* outcome) {
		return h().any_event("interaction", [&](const Dict& e) {
			return ev_s(e, "threat") == threat_cls && ev_s(e, "counter") == counter && ev_s(e, "outcome") == outcome;
		});
	}
	void fly(ActorState* p) {
		h().press(p, "tech");
		h().step(1);
		h().release(p, "tech");
	}
};
}  // namespace

FF_TEST(test_kit_air_sound, test_every_sound_move_has_a_def_and_a_binding) {
	Moves::ensure_ready();
	for (const char* idc : MOVES) {
		const std::string id = idc;
		check(Moves::defs().has(id), id + " registered");
		if (!Moves::defs().has(id)) continue;
		const Dict d = Moves::defs().get(id).as_dict();
		for (const char* k : {"name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"}) check(d.has(k), id + " has " + k);
		check(dint(d, "sub") == 3 && dint(d, "element") == 3, id + " is Air/Sound");
		const std::string slot = dstr(d, "slot");
		if (in_list(slot, {"strike", "thrust", "ground", "sweep", "tech", "guard"})) {
			check(d.has("tiers") && ddict(d, "tiers").has("t3"), id + " has tiers up to t3");
			check(d.has("counter") && d.has("threat"), id + " has counter + threat");
		}
		check(Moves::slot_of(3, 3, id) == slot, id + " bound to " + slot);
	}
	for (const char* slot : Sim::SLOTS) {
		const std::string id = Moves::resolve(3, 3, slot);
		check(!id.empty() && dint(Moves::defs().get(id).as_dict(), "sub", 0) == 3, std::string("Air/Sound ") + slot + " bound (" + id + ")");
	}
}

FF_TEST_F(test_kit_air_sound, AS, test_clap_shout_roar_resonance_tiers) {
	const double want[4][3] = {{3.0, 30.0, 8.0}, {7.0, 15.0, 14.0}, {10.0, 18.0, 22.0}, {12.0, 22.0, 32.0}};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _duel(2.5);
		SimHarness& h = this->h();
		const double hp0 = r->health;
		const double f0 = r->focus;
		run_move(a, "sound_clap", tier, tier == 0 ? 1 : 30, 50);
		const std::vector<Dict> fx = h.filter("fx", [](const Dict& e) { return ev_s(e, "fx") == "cone" && ev_s(e, "mat") == "sound"; });
		check(!fx.empty(), S("T", tier, ": a sound cone fx"));
		if (!fx.empty()) {
			near(ev_f(fx[0], "length"), want[tier][0], 1e-6, S("T", tier, " range"));
			near(ev_f(fx[0], "angle"), want[tier][1], 1e-6, S("T", tier, " half angle"));
			near(ev_f(fx[0], "power"), want[tier][2], 1e-6, S("T", tier, " pressure"));
		}
		check(r->health < hp0, S("T", tier, " hits the rival at 2.5 m (", r->health, ")"));
		if (tier == 0) check(h.has_event("status", "status", Value("dazed")), "Clap dazes (0.15 s)");
		if (tier >= 2) check(f0 - r->focus > 4.0 || r->focus < f0, S("T", tier, ": -8 Focus on hit (", f0, " -> ", r->focus, ")"));
		if (tier == 3) check(Status::has(*r, "deafened"), "Resonance deafens");
		check(a->action == nullptr, S("T", tier, " ends cleanly"));
		fx_catalogued(S("clap T", tier));
	}
}

FF_TEST_F(test_kit_air_sound, AS, test_costs_per_tier) {
	const double costs[4] = {4.0, 8.0, 12.0, 18.0};
	for (int tier = 0; tier < 4; ++tier) {
		auto [a, r] = _duel(14.0);
		(void)r;
		SimHarness& h = this->h();
		const double f0 = a->focus;
		double fmin = f0;
		h.w->start_action(*a, "sound_clap", h.it(a), D({{"slot", "strike"}, {"tier", tier}, {"charge_frozen", true}}));
		h.it(a).attack_held = tier > 0;
		for (int k = 0; k < 30; ++k) {
			h.step();
			fmin = minf(fmin, a->focus);
		}
		h.it(a).attack_held = false;
		for (int k = 0; k < 40; ++k) {
			h.step();
			fmin = minf(fmin, a->focus);
		}
		near(f0 - fmin, costs[tier], 1.2, S("T", tier, " cost"));
	}
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_disrupts_a_charge_when_it_reaches_the_cohesion) {
	// cohesion 6 + 4 x tier: a Clap (8) breaks a T0 charge, not a T1 one (10); a Shout (14) breaks a T1 charge, not a T3 one (18)
	struct Cs {
		int charge, clap;
		bool broken;
	};
	for (const Cs& cs : {Cs{0, 0, true}, Cs{1, 0, false}, Cs{1, 1, true}, Cs{3, 1, false}, Cs{3, 3, true}}) {
		auto [a, r] = _duel(2.5);
		SimHarness& h = this->h();
		r->is_dummy = false;
		const ActionRef inst = h.w->start_action(*r, "earth_attack", h.it(r), D({{"slot", "strike"}, {"tier", cs.charge}, {"charge_frozen", true}}));
		h.it(r).attack_held = true;
		h.step(30);
		check(r->action == inst && r->action != nullptr && r->action->phase == ActionPhase::Charge, S("setup: R is charging T", cs.charge));
		h.log.clear();
		run_move(a, "sound_clap", cs.clap, cs.clap == 0 ? 1 : 30, 14);
		const bool broken = h.has_event("disrupt", "actor", Value(r->id));
		check(broken == cs.broken, S("charge T", cs.charge, " vs sound tier ", cs.clap, ": ", cs.broken ? "disrupted" : "holds"));
		h.it(r).attack_held = false;
	}
}

FF_TEST_F(test_kit_air_sound, AS, test_roar_shatters_ice_and_a_stone_shot_and_resonance_a_heavy_one) {
	{
		auto [a, r] = _duel(10.0);
		(void)r;
		SimHarness& h = this->h();
		_ice_shard(V3(0, 1.25, a->pos.z - 2.0f));
		const Snap base = snap(*h.w);
		run_move(a, "sound_clap", 1, 30, 14);
		check(ix("ice", "sound", "shatter"), "Shout shatters ice (x2.5)");
		h.step(10);
		ledgers_ok(base, "ice shatter", 1e-5);
	}
	// a stone shot in flight: Roar (22 vs 17) shatters it
	auto [a, r] = _duel(12.0);
	SimHarness& h = this->h();
	Snap base;
	run_when(
	    a, "sound_clap", 2,
	    [&]() {
		    h.launch_at(a, "stone", 20.0, 6.0, Sim::AMBIENT_C, "", r, 5.0);
		    base = snap(*h.w);
	    },
	    30, 12);
	check(ix("stone", "sound", "shatter"), "Roar shatters a stone shot (resonance)");
	check(h.has_event("shatter"), "shatter event");
	ledgers_ok(base, "stone shatter", 1e-5);
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_lance_banks_off_the_cover_wall) {
	// A south of the cover wall, aiming at its face; the reflected pulse goes on to R where a straight shot would miss.
	{
		SimHarness& h = H(3);
		ActorState* a = h.actor("A", V3(-1.0, 0, 3.0), 0, Dict(), Sim::AIR);
		a->subs[Sim::AIR] = 3;
		ActorState* r = h.actor("R", V3(-6.5, 0, 3.0), 1, Dict(), Sim::EARTH);
		r->is_dummy = true;
		h.step(10);
		h.log.clear();
		h.aim(a, V3(-2.75, 0, -3.75).normalized());
		const double hp0 = r->health;
		run_move(a, "sound_lance", 0, 1, 30);
		check(h.has_event("ricochet"), "the pulse bounces off the wall (ricochet event)");
		check(r->health < hp0, S("and the reflected pulse hits R (", r->health, ")"));
		const std::vector<Dict> beams = h.filter("fx", [](const Dict& e) { return ev_s(e, "fx") == "beam" && ev_s(e, "mat") == "sound"; });
		check(beams.size() == 1 && beams[0].get("path").as_array().size() >= 3, "the beam fx carries the bent path");
	}
	// straight at R it would not need the wall; away from the wall with nothing in line: no hit
	auto [a2, r2] = _duel(10.0);
	this->h().aim(a2, V3(1, 0, 0));
	run_move(a2, "sound_lance", 0, 1, 30);
	check(r2->health == 100.0, "a lance aimed away from R misses");
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_lance_bounces_off_a_bulwark_and_the_arena_wall_and_passes_mist) {
	{
		SimHarness& h = H(3);
		ActorState* a = h.actor("A", V3(0, 0, 8), 0, Dict(), Sim::AIR);
		a->subs[Sim::AIR] = 3;
		ActorState* r = h.actor("R", V3(6, 0, 8), 1, Dict(), Sim::EARTH);
		r->is_dummy = true;
		// a stone wall to A's north; aim at it at 45 deg: reflects east toward R
		MatBody* wall = h.w->spawn_body(Mat::Stone, Form::Wall, 120.0, V3(3.0, 0, 3.0), "test");
		h.w->mass_ledger.ground_taken += 120.0;
		wall->wall_yaw = 0.0;
		wall->wall_half = V3(3.0, 1.0, 0.3);
		wall->wall_rise = 1.0;
		wall->static_body = true;
		wall->props.set("standing", 999.0);
		h.step(10);
		h.log.clear();
		h.aim(a, V3(3.0, 0, -4.7).normalized());
		const double hp0 = r->health;
		run_move(a, "sound_lance", 0, 1, 30);
		check(h.has_event("ricochet"), "reflected by the stone wall");
		check(r->health < hp0, "bank shot off a stone wall reaches R");
	}
	// mist in the line does not stop the pulse
	auto [a2, r2] = _duel(10.0);
	SimHarness& h = this->h();
	MatBody* mist = h.w->spawn_body(Mat::Water, Form::Cloud, 2.0, V3(0, 1.25, a2->pos.z - 3.0f), "test");
	h.w->mass_ledger.moisture_taken += 2.0;
	mist->max_life = 30.0;
	mist->gravity_scale = 0.0;
	run_move(a2, "sound_lance", 0, 1, 30);
	check(r2->health < 100.0, S("the lance passes through mist (and hits R: ", r2->health, ")"));
}

FF_TEST_F(test_kit_air_sound, AS, test_tremor_hum_knocks_over_cracks_walls_and_pops_stones) {
	{
		auto [a, r] = _duel(12.0);
		(void)r;
		SimHarness& h = this->h();
		const BodyRef wall = keep(h.w->spawn_body(Mat::Stone, Form::Wall, 60.0, V3(0, 0, a->pos.z - 6.0f), "test"));
		h.w->mass_ledger.ground_taken += 60.0;
		wall->wall_yaw = 0.0;
		wall->wall_half = V3(2.5, 0.75, 0.28);
		wall->wall_rise = 1.0;
		wall->static_body = true;
		wall->props.set("standing", 999.0);
		MatBody* stone = h.w->spawn_body(Mat::Stone, Form::Chunk, 8.0, V3(0, 0.2, a->pos.z - 3.0f), "test");
		h.w->mass_ledger.ground_taken += 8.0;
		stone->on_ground = true;
		const Snap base = snap(*h.w);
		run_move(a, "sound_tremor", 0, 1, 14);
		const std::vector<BodyRef> ws = bodies_tagged("tremor");
		check(ws.size() == 1 && ws[0]->form == Form::Wave && ws[0]->mat == Mat::Air, "an AIR wave (tag tremor)");
		if (!ws.empty()) near(dnum(ws[0]->props, "speed"), 18.0, 1e-6, "18 m/s");
		h.step(60);
		check(h.has_event("pop"), "the loose stone is popped into the air");
		check(wall->wall_damage > 0.0 || !wall->alive, S("the wall is cracked (damage ", wall->wall_damage, ")"));
		ledgers_ok(base, "tremor", 1e-4);
	}
	// knocks the rival over
	auto [a2, r2] = _duel(7.0);
	run_move(a2, "sound_tremor", 0, 1, 80);
	check(r2->balance < 80.0 || r2->health < 100.0, S("the rival is knocked about (balance ", r2->balance, " health ", r2->health, ")"));
}

FF_TEST_F(test_kit_air_sound, AS, test_echo_ring_reveals_the_hidden_and_interrupts_channels) {
	{
		auto [a, r] = _duel(3.0);
		SimHarness& h = this->h();
		r->is_dummy = false;
		Status::apply(*h.w, *r, "concealed", 5.0, 1.0, r->id);
		Status::apply(*h.w, *r, "fogwalk", 5.0, 1.0, r->id);
		h.w->start_action(*r, "earth_tech", h.it(r), D({{"slot", "tech"}, {"tier", 0}}));
		h.it(r).tech_held = true;
		h.step(20);
		check(r->action != nullptr && r->action->phase == ActionPhase::Channel, "setup: R holds a technique (channel)");
		run_move(a, "sound_echo_ring", 0, 1, 24);
		check(!Status::has(*r, "concealed") && !Status::has(*r, "fogwalk"), "hiding is revealed");
		check(Status::has(*r, "revealed"), "marked revealed");
		check(h.has_event("revealed", "actor", Value(r->id)), "revealed event");
		check(h.has_event("disrupt", "actor", Value(r->id)), "and the channel is interrupted (P 10 >= 6)");
		h.it(r).tech_held = false;
	}
	auto [fa, fr] = _duel(9.0);
	run_move(fa, "sound_echo_ring", 0, 1, 24);
	check(fr->health == 100.0, "outside r 4 m: nothing");
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_barrier_shatters_ice_and_a_perfect_one_throws_sound_back) {
	{
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		h.press(a, "guard");
		h.step(30);
		const BodyRef shard = _ice_shard(a->chest() + a->forward() * 6.0f);
		shard->vel = (a->chest() - shard->pos).normalized() * 24.0f;
		shard->attack_id = h.w->new_attack_id();
		shard->attack_owner = r->id;
		shard->hit_set.add(r->id);
		const Snap base = snap(*h.w);
		h.step(40);
		check(ix("ice", "barrier_sound", "shatter"), "ice shatters against the barrier");
		check(a->health == 100.0, "A is unhurt");
		ledgers_ok(base, "barrier ice", 1e-5);
		h.release(a, "guard");
		h.step(30);
	}
	// Echo Return: a perfect guard against sound hits the source with its own sound
	auto [a2, r2] = _duel(4.0);
	SimHarness& h = this->h();
	r2->is_dummy = false;
	h.press(a2, "guard");
	h.step(2);
	const double hp0 = r2->health;
	AgentRef v = Agent::of_volume(h.w, r2, nullptr, "sound", r2->chest(), (a2->chest() - r2->chest()).normalized(), D({{"P", 10.0}}));
	const int aid = h.w->new_attack_id();
	h.w->hit_actor(*a2,
	               D({{"attacker", r2->id}, {"attack_id", aid}, {"damage", 10.0}, {"balance", 20.0}, {"knock", V3(0, 0, 4.0)}, {"kind", "sound"},
	                  {"from", r2->chest()}, {"power", 10.0}}),
	               v);
	check(a2->health == 100.0, "Echo Return: A takes nothing");
	check(r2->health < hp0 || h.has_event("reflect"), S("and the sound goes back at its source (R ", r2->health, ")"));
}

FF_TEST_F(test_kit_air_sound, AS, test_thunder_step_and_ground_ping_from_the_guard) {
	{
		auto [a, r] = _duel(5.0);
		SimHarness& h = this->h();
		r->pos = a->pos + V3(0, 0, -3.5);
		h.press(a, "guard");
		h.step(20);
		h.flick(a, "guard", static_cast<int>(Gesture::Up));
		h.step(3);
		check(a->action != nullptr && a->action->id == "sound_thunder_step", "guard flick up = Thunder Step");
		h.release(a, "guard");
		const double z0 = a->pos.z;
		h.step(30);
		check(z0 - a->pos.z > 2.5, S("dashes ~4 m forward (", z0 - a->pos.z, ")"));
		check(r->health < 100.0 || r->balance < 100.0, "and booms into the rival");
	}
	// Ground Ping
	auto [a2, r2] = _duel(8.0);
	SimHarness& h = this->h();
	r2->pos = a2->pos + V3(0, 0, -5.0);
	Status::apply(*h.w, *r2, "concealed", 6.0, 1.0, r2->id);
	const BodyRef surge = keep(h.w->spawn_body(Mat::Sand, Form::Wave, 14.0, V3(a2->pos.x, 0, a2->pos.z - 7.0f), "test"));
	h.w->mass_ledger.ground_taken += 14.0;
	surge->tag = "sand_surge";
	surge->wave_dir = V3(0, 0, 1);
	surge->wave_budget = 14.0;
	surge->wave_width = 2.0;
	surge->power = 8.0;
	surge->vel = V3(0, 0, 7.5);
	surge->props.set("speed", 7.5);
	surge->attack_id = h.w->new_attack_id();
	surge->attack_owner = r2->id;
	surge->hit_set.add(r2->id);
	const Snap base = snap(*h.w);
	h.press(a2, "guard");
	h.step(20);
	h.flick(a2, "guard", static_cast<int>(Gesture::Down));
	h.step(3);
	check(a2->action != nullptr && a2->action->id == "sound_ping", "guard flick down = Ground Ping");
	h.release(a2, "guard");
	h.step(20);
	check(h.has_event("wave_disrupted") || surge->form != Form::Wave, "the sand surge is stilled by the ping");
	check(!Status::has(*r2, "concealed"), "the burrower is revealed");
	ledgers_ok(base, "ping", 1e-5);
}

FF_TEST_F(test_kit_air_sound, AS, test_flight_hovers_keeps_every_attack_and_ends_with_a_glide) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	const double f0 = a->focus;
	fly(a);
	h.step(60);
	check(Status::has(*a, "flight") && a->flying && a->stance == "flight", "Flight is on");
	check(a->pos.y > 1.9f && a->pos.y < 2.5f, S("rises to ~2.2 m (", a->pos.y, ")"));
	check(a->action == nullptr, "the flight move itself is over: all attacks are usable");
	check(!zones_tagged("flight_field").empty(), "driven by a flight field attached to the fighter");
	check(f0 - a->focus > 6.0, S("paid 6 + 10/s upkeep (", f0 - a->focus, ")"));
	// ground lines pass under (a tagged wave under the flyer does not hit)
	MatBody* wave = h.w->spawn_body(Mat::Air, Form::Wave, 0.0, V3(a->pos.x, 0, a->pos.z + 2.0f), "test");
	wave->tag = "dust_line";
	wave->wave_dir = V3(0, 0, -1);
	wave->wave_budget = 6.0;
	wave->wave_width = 2.0;
	wave->props.set("speed", 8.0);
	wave->attack_id = h.w->new_attack_id();
	wave->attack_owner = r->id;
	wave->damage = 10.0;
	wave->balance_damage = 30.0;
	wave->hit_set.add(r->id);
	const double hp0 = a->health;
	h.step(40);
	check(a->health == hp0, "a ground line passes under a flyer");
	// an attack works while flying
	h.press(a, "attack");
	h.step(1);
	h.release(a, "attack");
	h.step(2);
	check(a->action != nullptr && a->action->id == "sound_clap", "Clap while flying");
	h.step(40);
	// press again: land
	h.press(a, "tech");
	h.step(2);
	h.release(a, "tech");
	h.step(14);
	check(!Status::has(*a, "flight") && !a->flying, "pressing again ends the flight");
	h.step(120);
	check(a->grounded && a->pos.y < 0.2f, S("and glides down to the ground (", a->pos.y, ")"));
	check(zones_tagged("flight_field").empty(), "the field is gone");
}

FF_TEST_F(test_kit_air_sound, AS, test_flight_is_vulnerable_to_gusts_and_a_downdraft_and_costs_focus) {
	// a flyer loses x1.5 balance to the same Palm Gust
	std::vector<double> losses;
	for (bool flying : {false, true}) {
		SimHarness& h = H(3);
		ActorState* a = h.actor("A", V3(0, 0, 3), 0, Dict(), Sim::AIR);
		ActorState* r = h.actor("R", V3(0, 0, 0), 1, Dict(), Sim::AIR);
		a->subs[Sim::AIR] = 0;
		r->subs[Sim::AIR] = 3;
		r->is_dummy = !flying;
		h.step(20);
		if (flying) {
			r->is_dummy = false;
			fly(r);
			h.step(60);
			check(Status::has(*r, "flight"), "R flies");
		}
		const double b0 = r->balance;
		run_move(a, "air_attack", 0, 1, 20);
		losses.push_back(b0 - r->balance);
	}
	check(losses[1] > losses[0] * 1.3, S("a flyer loses x1.5 balance to the gust (", losses[1], " vs ", losses[0], ")"));
	{
		// a Downdraft slams a flyer to the ground and ends the flight
		auto [a2, r2] = duel(0, Sim::AIR, 3, 3.0);
		SimHarness& h = this->h();
		r2->subs[Sim::AIR] = 3;
		r2->is_dummy = false;
		fly(r2);
		h.step(60);
		check(r2->flying, "R flies");
		h.press(a2, "guard");
		h.step(20);
		h.flick(a2, "guard", static_cast<int>(Gesture::Down));
		h.release(a2, "guard");
		h.step(20);
		check(!r2->flying && !Status::has(*r2, "flight"), "Downdraft ends the flight");
	}
	// no Focus: the flight ends
	auto [a3, r3] = _duel(10.0);
	(void)r3;
	SimHarness& h = this->h();
	fly(a3);
	h.step(30);
	a3->focus = 0.4;
	h.step(30);
	check(!a3->flying && h.has_event("insufficient"), "out of Focus: the flight ends");
}

FF_TEST_F(test_kit_air_sound, AS, test_boom_step_and_hover) {
	{
		auto [a, r] = _duel(4.0);
		SimHarness& h = this->h();
		r->pos = a->pos + V3(0, 0, -5.5);
		h.it(a).move = V3(0, 0, -1);
		const double z0 = a->pos.z;
		h.press(a, "evade");
		h.step(30);
		check(a->action == nullptr || a->action->id == "sound_boom_step", "the evade is the Boom Step");
		check(h.has_event("evade", "move", Value("sound_boom_step")), "evade event");
		check(z0 - a->pos.z > 4.5, S("a long dash (", z0 - a->pos.z, " m)"));
		check(r->balance < 100.0 || r->health < 100.0 || r->vel.length() > 0.0f, "ends in a boom that hits what is near");
	}
	// Hover: hold the current height
	auto [a2, r2] = _duel(10.0);
	(void)r2;
	SimHarness& h = this->h();
	a2->pos.y = 2.0f;
	a2->grounded = false;
	a2->vel.y = 0.0f;
	h.press(a2, "evade");
	h.it(a2).evade_held = true;
	h.step(16);
	check(a2->action != nullptr && a2->action->id == "sound_hover", "held 0.2 s: Hover");
	h.step(60);
	check(a2->pos.y > 1.5f && a2->pos.y < 2.5f, S("holds its height (", a2->pos.y, ")"));
	h.it(a2).evade_held = false;
	h.step(30);
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_column_cells_at_reference_powers) {
	auto [a, r] = _duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	CombatWorld& w = *h.w;
	auto clap = [&](int tier) { return Agent::of_move(h.w, a, "sound_clap", tier, false); };
	check(predict(threat(w, "ice", 9.0, 6.0), clap(0)) == "air_shatter", "Clap shatters ice (x2.5)");
	check(predict(threat(w, "glass", 9.0, 6.0), clap(0)) == "air_shatter", "and glass");
	check(predict(threat(w, "stone", 17.0, 20.0), clap(2)) == "air_shatter", "Roar T2 (22 vs 17) shatters a stone shot");
	check(predict(threat(w, "stone", 17.0, 20.0), clap(1)) == "bend", "Shout T1 (14 vs 17, 0.82) only bends it");
	check(predict(threat(w, "stone", 17.0, 20.0), clap(0)) == "pass", "Clap T0 (8 vs 17, 0.47) does nothing");
	check(predict(threat(w, "stone_heavy", 31.5, 45.0), clap(3)) == "air_shatter", "Resonance T3 (32 vs 31.5) shatters a heavy stone");
	check(predict(threat(w, "hot_rock", 26.8, 20.0), clap(2)) == "air_shatter", "hot rock is brittle (x1.3): Roar shatters it");
	for (const char* t : {"boulder", "magma", "lava_wave"})
		check(predict(threat(w, t, 60.0, 50.0), clap(3)) == "pass", std::string(t) + ": FAIL (sound does nothing)");
	check(predict(threat(w, "sand", 10.0, 8.0), clap(2)) == "pass", "sand absorbs sound");
	check(predict(threat(w, "sand_cloud", 8.0, 6.0), clap(3)) == "weaken", "Roar T3 weakens a sand cloud");
	check(predict(threat(w, "mist", 3.0, 2.0), clap(2)) == "air_compress", "Roar rains out mist");
	check(predict(threat(w, "mist", 3.0, 2.0), clap(0)) == "pass", "a Clap does not");
	check(predict(threat(w, "water", 9.6, 12.0), clap(2)) == "weaken", "Roar atomises a stream");
	check(predict(threat(w, "water", 9.6, 12.0), clap(1)) == "pass", "Shout passes through water");
	check(predict(threat(w, "flame", 8.0, 0.0, "H"), clap(2)) == "extinguish", "Roar blows a small flame out");
	check(predict(threat(w, "blue_fire", 16.0, 0.0, "H"), clap(3)) == "pass", "blue fire does not care");
	check(predict(threat(w, "lightning", 24.0, 0.0, "E"), clap(3)) == "pass", "lightning passes");
	check(predict(threat(w, "blast", 16.0, 0.0, "P"), clap(2)) == "weaken", "sound weakens a blast (x0.6)");
	check(predict(threat(w, "tornado", 30.0, 0.0, "P"), clap(3)) == "air_shrink", "Resonance weakens a tornado (x0.6)");
	check(predict(threat(w, "vacuum", 18.0, 0.0, "P"), clap(3)) == "pass", "no medium in a vacuum");
	// the barrier
	AgentRef bar = Agent::of_move(h.w, a, "sound_barrier", 0, false);
	check(bar->ccls == "barrier_sound" && std::fabs(bar->power - 12.0) < 1e-6, "Sound Barrier CP 12");
	check(predict(threat(w, "ice", 9.0, 6.0), bar) == "air_shatter", "ice shatters on it");
	check(predict(threat(w, "stone", 17.0, 20.0), bar) == "bend", "a stone shot is only bent (0.7)");
	check(predict(threat(w, "boulder", 110.0, 200.0), bar) == "overwhelm", "a boulder ignores it");
	check(predict(threat(w, "vacuum", 18.0, 0.0, "P"), bar) == "overwhelm", "a vacuum nullifies it");
	check(predict(threat(w, "sound", 10.0, 0.0, "P"), bar) == "absorb", "sound is cancelled");
	AgentRef perf = Agent::of_move(h.w, a, "sound_barrier", 0, true);
	check(predict(threat(w, "sound", 10.0, 0.0, "P"), perf) == "reflect", "Echo Return: reflected");
	// the ping
	check(predict(threat(w, "sand_surge", 20.0, 14.0), Agent::of_move(h.w, a, "sound_ping", 1, false)) == "air_still",
	      "Ground Ping T1 (x1.5 on a sand surge) stills it");
	check(predict(threat(w, "lava_wave", 27.3, 20.0), Agent::of_move(h.w, a, "sound_ping", 3, false)) == "pass", "but lava is none of its business");
	check(predict(threat(w, "sand_surge", 34.0, 14.0), Agent::of_move(h.w, a, "sound_ping", 0, false)) == "weaken",
	      "a big surge is only weakened by a weak ping (partial band)");
}

FF_TEST_F(test_kit_air_sound, AS, test_sound_kit_is_deterministic_and_ledgers_balance) {
	std::vector<std::string> hashes;
	for (int k = 0; k < 2; ++k) {
		auto [a, r] = _duel(8.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		h.launch_at(a, "stone", 20.0, 8.0, Sim::AMBIENT_C, "", r, 6.0);
		run_move(a, "sound_clap", 2, 30, 30);
		run_move(a, "sound_lance", 1, 20, 30);
		run_move(a, "sound_tremor", 1, 1, 60);
		ledgers_ok(base, "sound exchange", 1e-5);
		finite_world("sound");
		hashes.push_back(S(a->pos.x, ",", a->pos.y, ",", a->pos.z, "|", r->pos.x, ",", r->pos.z, "|", h.w->bodies.size(), "|", ftos(a->focus, 4), "|",
		                   ftos(r->health, 4)));
	}
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: " + hashes[0] + " vs " + hashes[1]);
}
