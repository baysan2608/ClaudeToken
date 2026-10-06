// Port of game/tests/sim/test_kit_air_vacuum.gd: Air / Vacuum (sub 2): Pressure Palm -> Air Cannon -> Implode -> Collapse,
// Suction Line, Pressure Mine, Vacuum Arc, Null Bubble (+ Vacuum Catch), Pressure Wave, Anchor, Vacuum Well (+ collapse
// and the air-inrush flag), Pressure Hop, Slipstream, the Vacuum column. Clip existence checks are not ported.
#include "ff_test.h"
#include "kit_air_util.h"
#include "sim_harness.h"

#include "Combat/Kits/Air/AirUtil.h"
#include "Sim/Status.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>

using namespace ff;
using namespace fft;

namespace {
const char* const MOVES[] = {"vacuum_palm", "vacuum_suction", "vacuum_mine", "vacuum_arc", "vacuum_bubble", "vacuum_wave", "vacuum_anchor", "vacuum_well",
                             "vacuum_hop", "vacuum_slipstream"};

struct AVa : AirCase {
	std::pair<ActorState*, ActorState*> _duel(double dist = 10.0) { return duel(2, Sim::EARTH, 3, dist); }
	BodyRef _fire_body(Vec3 pos, double hu = 120.0, ActorState* owner = nullptr) {
		SimHarness& hh = h();
		MatBody* b = hh.w->spawn_body(Mat::Fire, Form::Chunk, 0.5, pos, "test");
		b->heat_payload = hu;
		hh.w->ledger.generated += hu;
		b->tag = "fireball";
		b->gravity_scale = 0.0;
		b->attack_id = hh.w->new_attack_id();
		b->attack_owner = owner != nullptr ? owner->id : -1;
		if (owner != nullptr) b->hit_set.add(owner->id);
		return keep(b);
	}
	static double flat(Vec3 a, Vec3 b) { return static_cast<double>(Vec2(a.x - b.x, a.z - b.z).length()); }
	std::string predict(const AgentRef& th, const AgentRef& c) { return Interactions::predict(h().w, *th, *c).outcome; }
	bool fx_any(const char* fx, const char* mat) {
		return h().any_event("fx", [&](const Dict& e) { return ev_s(e, "fx") == fx && ev_s(e, "mat") == mat; });
	}
};
}  // namespace

FF_TEST(test_kit_air_vacuum, test_every_vacuum_move_has_a_def_and_a_binding) {
	Moves::ensure_ready();
	for (const char* idc : MOVES) {
		const std::string id = idc;
		check(Moves::defs().has(id), id + " registered");
		if (!Moves::defs().has(id)) continue;
		const Dict d = Moves::defs().get(id).as_dict();
		for (const char* k : {"name", "desc", "slot", "sub", "element", "startup", "recovery", "cost", "anim", "fx", "ai"}) check(d.has(k), id + " has " + k);
		check(dint(d, "sub") == 2 && dint(d, "element") == 3, id + " is Air/Vacuum");
		const std::string slot = dstr(d, "slot");
		if (in_list(slot, {"strike", "thrust", "ground", "sweep", "tech"})) {
			check(d.has("tiers") && ddict(d, "tiers").has("t3"), id + " has tiers up to t3");
			check(d.has("counter") && d.has("threat"), id + " has counter + threat");
		}
		check(Moves::slot_of(3, 2, id) == slot, id + " bound to " + slot);
	}
	for (const char* slot : Sim::SLOTS) {
		const std::string id = Moves::resolve(3, 2, slot);
		check(!id.empty() && dint(Moves::defs().get(id).as_dict(), "sub", 0) == 2, std::string("Air/Vacuum ") + slot + " bound (" + id + ")");
	}
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_pressure_palm_air_cannon_implode_collapse) {
	{
		auto [a, r] = _duel(6.0);
		r->pos = a->pos + V3(0, 0, -1.8);
		const double hp0 = r->health;
		run_move(a, "vacuum_palm", 0, 1, 24);
		check(r->health < hp0 && r->balance < 100.0, S("T0 Pressure Palm: point-blank burst (balance ", r->balance, ")"));
		check(fx_any("burst", "vacuum"), "burst fx (vacuum)");
	}
	{
		// T1 Air Cannon: a pressure bullet 12 m
		auto [a, r] = _duel(10.0);
		const double hp0 = r->health;
		run_move(a, "vacuum_palm", 1, 30, 30);
		check(r->health < hp0, S("T1 Air Cannon reaches 10 m (", r->health, ")"));
		check(fx_any("beam", "vacuum"), "beam fx");
	}
	// T2 Implode / T3 Collapse
	for (int tier : {2, 3}) {
		auto [a, r] = _duel(9.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		const double b0 = r->balance;
		run_move(a, "vacuum_palm", tier, 30, 13);
		const std::vector<BodyRef> wells = zones_tagged("vacuum_well");
		check(wells.size() == 1 && wells[0]->owner == a->id, S("T", tier, ": a vacuum point"));
		if (wells.size() == 1) {
			near(wells[0]->power, tier == 2 ? 22.0 : 30.0, 1e-6, S("T", tier, " power"));
			near(wells[0]->zone_radius, tier == 2 ? 3.0 : 4.0, 1e-6, S("T", tier, " radius"));
		}
		h.step(40);
		check(zones_tagged("vacuum_well").empty(), S("T", tier, ": it collapsed after ~0.4 s"));
		check(h.has_event("collapse"), "collapse event");
		check(!zones_tagged("inrush").empty() || h.has_event("inrush"), "and left an air inrush");
		check(r->balance < b0 || r->health < 100.0, S("T", tier, " crushed the rival (balance ", r->balance, " health ", r->health, ")"));
		ledgers_ok(base, S("implode T", tier), 1e-5);
		fx_catalogued(S("palm T", tier));
	}
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_suction_line_pulls_a_fighter_and_yanks_a_light_projectile) {
	{
		auto [a, r] = _duel(9.0);
		const double d0 = a->pos.distance_to(r->pos);
		run_move(a, "vacuum_suction", 0, 1, 40);
		const double d1 = a->pos.distance_to(r->pos);
		check(d0 - d1 > 1.2 && d0 - d1 < 3.2, S("pulls the rival ~2 m toward A (", d0, " -> ", d1, ")"));
	}
	{
		// a 6 kg metal plate in flight is yanked into the hand (<= 12 kg)
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		BodyRef plate;
		Snap base;
		run_when(
		    a, "vacuum_suction", 0,
		    [&]() {
			    plate = keep(h.launch_at(a, "metal", 6.0, 8.0, Sim::AMBIENT_C, "", r, 6.0));
			    h.w->mass_ledger.metal_taken += 6.0;
			    base = snap(*h.w);
		    },
		    1, 20);
		check(h.w->held(*a) == plate.get(), "the 6 kg plate is reclaimed into A's hand");
		ledgers_ok(base, "yank", 1e-6);
	}
	// a 20 kg stone (> 12 kg) is not yanked
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	BodyRef stone;
	run_when(a, "vacuum_suction", 0, [&]() { stone = keep(h.launch_at(a, "stone", 20.0, 8.0, Sim::AMBIENT_C, "", r, 6.0)); }, 1, 20);
	check(h.w->held(*a) != stone.get(), "a 20 kg stone is too heavy to yank");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_suction_line_pulls_you_to_a_wall) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	r->pos = V3(8, 0, -8);   // out of the line
	const BodyRef wall = keep(h.w->spawn_body(Mat::Stone, Form::Wall, 120.0, a->pos + a->forward() * 6.0f, "test"));
	h.w->mass_ledger.ground_taken += 120.0;
	wall->wall_yaw = a->facing;
	wall->wall_half = V3(1.1, 0.75, 0.28);
	wall->wall_rise = 1.0;
	wall->static_body = true;
	wall->props.set("standing", 999.0);
	h.step(5);
	const double d0 = a->pos.distance_to(wall->pos);
	h.aim(a, V3(0, 0, -1));
	run_move(a, "vacuum_suction", 0, 1, 40);
	check(h.has_event("grapple"), "no fighter in line: the beam anchors on the wall");
	check(a->pos.distance_to(wall->pos) < d0 - 2.0, S("A is pulled toward the wall (", d0, " -> ", a->pos.distance_to(wall->pos), ")"));
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_pressure_mine_launches_the_first_fighter_who_steps_on_it) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	run_move(a, "vacuum_mine", 0, 1, 14);
	const std::vector<BodyRef> ms = zones_tagged("mine");
	check(ms.size() == 1 && ms[0]->owner == a->id && std::fabs(ms[0]->zone_radius - 1.2) < 1e-6, "a mine (zone r 1.2 m)");
	if (ms.empty()) return;
	check(ms[0]->max_life == 10.0, "lives 10 s");
	h.step(40);
	check(a->health == 100.0 && !h.has_event("mine_burst"), "it ignores its owner and stays armed");
	r->pos = ms[0]->pos + V3(0.3, 0, 0);
	h.step(4);
	check(h.has_event("mine_burst"), "stepped on: it bursts");
	h.step(10);
	check(r->pos.y > 0.4f || r->vel.y > 3.0f || r->health < 100.0, S("the rival is launched (vy ", r->vel.y, " y ", r->pos.y, ")"));
	check(zones_tagged("mine").empty(), "and the mine is spent");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_vacuum_arc_snuffs_flames_and_ledgers_balance) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	const BodyRef ball = _fire_body(V3(0, 1.2, a->pos.z - 2.5f), 120.0, r);
	const Snap base = snap(*h.w);
	run_move(a, "vacuum_arc", 0, 1, 40);
	check(!ball->alive || ball->heat_payload < 1.0, "the arc puts the fireball out (x2)");
	check(h.any_event("interaction", [](const Dict& e) { return ev_s(e, "counter") == "vacuum" && ev_s(e, "outcome") == "extinguish"; }),
	      "interaction vacuum x flame -> extinguish");
	ledgers_ok(base, "arc", 1e-5);
	check(a->action == nullptr, "ends cleanly");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_null_bubble_snuffs_fire_nullifies_sound_and_lets_solids_pass) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	h.press(a, "guard");
	h.step(20);
	const std::vector<BodyRef> bs = zones_tagged("null_bubble");
	check(bs.size() == 1 && bs[0]->owner == a->id && std::fabs(bs[0]->zone_radius - 1.8) < 1e-6, "Null Bubble: a vacuum shell r 1.8 m");
	check(bs.size() == 1 && dbool(bs[0]->props, "barrier", false), "it insulates (blocks bolts)");
	const BodyRef fire = _fire_body(V3(0, 1.2, a->pos.z - 5.0f), 100.0, r);
	fire->vel = V3(0, 0, 8.0);
	const Snap base = snap(*h.w);
	h.step(40);
	check(!fire->alive, "a fireball dies at the shell");
	check(a->health == 100.0, "A is unhurt");
	ledgers_ok(base, "bubble vs fire", 1e-5);
	// a stone passes through (solids pass) and hits - a plain bubble is not a wall
	const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 14.0, Sim::AMBIENT_C, "", r, 6.0));
	h.step(40);
	check(stone->attack_id == 0 && a->health < 100.0 && stone->captured_by < 0, "a stone passes the bubble and lands (solids pass)");
	// steam collapses into water
	h.log.clear();
	const BodyRef steam = keep(h.w->spawn_body(Mat::Steam, Form::Cloud, 0.5, a->pos + V3(0, 1.2, -0.5), "test"));
	h.w->mass_ledger.vapor += 0.0;
	steam->max_life = 5.0;
	const double w0 = h.w->water_mass();
	h.step(20);
	check(steam->mat == Mat::Water && steam->form != Form::Cloud, "steam collapses into water drops");
	near(h.w->water_mass(), w0, 1e-6, "water mass conserved");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_vacuum_catch_spits_a_light_projectile_back) {
	auto [a, r] = _duel(12.0);
	SimHarness& h = this->h();
	const BodyRef st = keep(h.launch_at(a, "stone", 20.0, 12.0, Sim::AMBIENT_C, "", r, 4.8));
	h.step(4);
	h.press(a, "guard");
	h.step(40);
	check(h.has_event("vacuum_catch"), "Vacuum Catch");
	check(st->attack_owner == a->id && st->attack_id != 0, "the stone is A's attack now");
	check(st->vel.dot(r->pos - st->pos) > 0.0f || st->attack_id == 0, "spat back at the thrower");
	check(a->health == 100.0, "A is unhurt");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_pressure_wave_and_anchor_from_the_null_bubble) {
	{
		auto [a, r] = _duel(10.0);
		SimHarness& h = this->h();
		r->pos = a->pos + V3(0, 0, -2.0);
		h.press(a, "guard");
		h.step(20);
		h.flick(a, "guard", static_cast<int>(Gesture::Up));
		h.step(3);
		check(a->action != nullptr && a->action->id == "vacuum_wave", "guard flick up = Pressure Wave");
		h.release(a, "guard");
		const double d0 = a->pos.distance_to(r->pos);
		h.step(40);
		check(r->pos.distance_to(a->pos) > d0 + 0.8 || r->health < 100.0, S("the ring shoves the rival back (", d0, " -> ", r->pos.distance_to(a->pos), ")"));
		check(zones_tagged("null_bubble").empty(), "the bubble collapsed into the wave");
	}
	// Anchor: immune to knockback / pull / lift
	auto [a, r] = _duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	h.press(a, "guard");
	h.step(20);
	h.flick(a, "guard", static_cast<int>(Gesture::Down));
	h.step(8);
	check(a->action != nullptr && a->action->id == "vacuum_anchor" && a->anchored, "guard flick down = Anchor (stance anchored)");
	check(Status::immune(*a, "lift") && Status::immune(*a, "pull") && Status::immune(*a, "knockback"), "immune to lift, pull and knockback");
	const Vec3 p0 = a->pos;
	const int aid = h.w->new_attack_id();
	h.w->hit_actor(*a, D({{"attacker", 99}, {"attack_id", aid}, {"damage", 4.0}, {"balance", 10.0}, {"knock", V3(0, 0, 9.0)}, {"kind", "air"},
	                      {"from", a->pos + V3(0, 0, -2)}}));
	h.step(20);
	check(a->pos.distance_to(p0) < 0.8f, S("anchored: not knocked around (", a->pos.distance_to(p0), " m)"));
	h.release(a, "guard");
	h.step(30);
	check(!a->anchored, "released: anchor off");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_vacuum_well_pulls_compresses_drags_and_collapses_with_an_inrush) {
	auto [a, r] = _duel(10.0);
	SimHarness& h = this->h();
	h.aim(a, V3(0, 0, -1));
	h.press(a, "tech");
	h.step(30);
	const std::vector<BodyRef> wells = zones_tagged("vacuum_well");
	check(wells.size() == 1 && wells[0]->owner == a->id, "Vacuum Well at the aim point");
	if (wells.empty()) return;
	const BodyRef well = wells[0];
	const double dist = flat(well->pos, a->pos);
	check(dist > 6.0 && dist <= 9.5, S("9 m range (", dist, ")"));
	// steam -> water (booked), sand -> sandstone (booked)
	const BodyRef steam = keep(h.w->spawn_body(Mat::Steam, Form::Cloud, 0.6, well->pos + V3(0.5, 1.2, 0), "test"));
	steam->max_life = 8.0;
	const BodyRef sand = keep(h.w->spawn_body(Mat::Sand, Form::Cloud, 3.0, well->pos + V3(-0.5, 1.2, 0), "test"));
	h.w->mass_ledger.ground_taken += 3.0;
	sand->max_life = 8.0;
	const Snap base = snap(*h.w);
	h.step(30);
	check(steam->mat == Mat::Water, "steam is compressed into water");
	check(sand->mat == Mat::Stone && sand->tag == "sandstone", "sand is compressed into sandstone");
	ledgers_ok(base, "well compress", 1e-5);
	// fire in the well goes out
	const BodyRef fire = _fire_body(well->pos + V3(0.4, 1.2, 0.4), 60.0, r);
	h.step(20);
	check(!fire->alive, "fire is suppressed in the well");
	// a stone is pulled out of the air and hangs in the well
	const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 12.0, Sim::AMBIENT_C, "", r, 0.0));
	stone->pos = well->pos + V3(2.5, 1.2, 0);
	stone->vel = V3(-4, 0, 0);
	h.step(40);
	check(stone->captured_by == well->id, "a stone is caught in the well");
	// a fighter near it is dragged in
	r->pos = well->pos + V3(3.5, 0, 0);
	const double d0 = flat(r->pos, well->pos);
	h.step(30);
	const double d1 = flat(r->pos, well->pos);
	check(d1 < d0 - 0.5, S("the rival is dragged toward the well (", d0, " -> ", d1, ")"));
	// release: collapse + inrush
	h.log.clear();
	h.release(a, "tech");
	h.step(4);
	check(h.has_event("collapse"), "release collapses the well");
	check(!zones_tagged("inrush").empty(), "and leaves an air-inrush zone");
	check(AirUtil::inrush_tick_at(*h.w, well->pos) >= 0, "the inrush flag Fire reads (AirUtil.inrush_tick_at)");
	check(stone->captured_by < 0, "the captured stone is released");
	h.step(60);
	check(zones_tagged("inrush").empty(), "the inrush fades (0.6 s)");
	check(a->action == nullptr, "ends cleanly");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_pressure_hop_and_slipstream) {
	{
		auto [a, r] = _duel(10.0);
		(void)r;
		SimHarness& h = this->h();
		h.it(a).move = V3(1, 0, 0);
		h.press(a, "evade");
		h.step(4);
		check(a->action != nullptr && a->action->id == "vacuum_hop", "the evade is the Pressure Hop");
		double peak = 0.0;
		for (int k = 0; k < 90; ++k) {
			h.step();
			peak = maxf(peak, a->pos.y);
		}
		check(peak > 0.8, S("leaps up (peak ", peak, " m)"));
		check(a->grounded && a->action == nullptr, "lands, action over");
	}
	// Slipstream
	auto [a, r] = _duel(14.0);
	SimHarness& h = this->h();
	h.it(a).move = V3(0, 0, 1);
	h.step(30);
	h.press(a, "evade");
	h.it(a).evade_held = true;
	h.step(16);
	check(a->action != nullptr && a->action->id == "vacuum_slipstream", "held 0.2 s: Slipstream");
	const BodyRef stone = keep(h.launch_at(a, "stone", 20.0, 12.0, Sim::AMBIENT_C, "", r, 2.5));
	stone->pos = a->pos + V3(0, 1.2, 2.5);
	stone->vel = V3(0, 0, 12.0);
	h.step(10);
	check(stone->vel.length() < 12.0f * 0.9f, S("a projectile behind the runner is slowed (", stone->vel.length(), " m/s)"));
	h.it(a).evade_held = false;
	h.it(a).move = Vec3();
	h.step(30);
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_vacuum_column_cells_at_reference_powers) {
	auto [a, r] = _duel(10.0);
	(void)r;
	SimHarness& h = this->h();
	CombatWorld& w = *h.w;
	AgentRef bub = Agent::of_move(h.w, a, "vacuum_bubble", 0, false);
	check(bub->ccls == "bubble_null" && std::fabs(bub->power - 14.0) < 1e-6, "Null Bubble CP 14");
	check(predict(threat(w, "flame", 8.0, 0.0, "H"), bub) == "extinguish", "flame: EXT (x2)");
	check(predict(threat(w, "blast", 24.0, 0.0, "P"), bub) == "extinguish", "a 24 PU combustion: ratio 1.17 EXT");
	check(predict(threat(w, "blast", 38.0, 0.0, "P"), bub) == "weaken", "a T3 detonation (38) only weakens: ratio 0.74");
	check(predict(threat(w, "sound", 16.0, 0.0, "P"), bub) == "absorb", "sound x3: nullified");
	check(predict(threat(w, "sound", 32.0, 0.0, "P"), bub) == "absorb", "Resonance 32: 42 vs 32 still nullified");
	check(predict(threat(w, "sound", 60.0, 0.0, "P"), bub) == "weaken", "but 60 PU of sound only weakens");
	// lightning: E <= 1.5 x CP = 21 is blocked, above it the bolt is weakened (bands)
	check(predict(threat(w, "lightning", 20.0, 0.0, "E"), bub) == "block", "a 20 PU bolt (<= 21) is blocked");
	const IxResult big = Interactions::predict(h.w, *threat(w, "lightning", 30.0, 0.0, "E"), *bub);
	check(big.outcome == "weaken" && big.band == "partial", S("a 30 PU bolt: ratio ", big.ratio, " -> weakened"));
	const std::string sky = predict(threat(w, "lightning", 52.0, 0.0, "E"), bub);
	check(sky == "weaken" || sky == "pass", "Skybreak (52) is not stopped");
	for (const char* t : {"stone", "stone_heavy", "boulder", "lava_wave", "water", "sand_surge", "metal"})
		check(predict(threat(w, t, 17.0, 20.0), bub) == "pass", std::string(t) + " passes the vacuum");
	check(predict(threat(w, "mist", 3.0, 2.0), bub) == "air_compress", "mist collapses");
	check(predict(threat(w, "steam", 6.0, 1.0), bub) == "air_compress", "steam collapses");
	// a perfect bubble spits a light projectile back
	AgentRef perfect = Agent::of_move(h.w, a, "vacuum_bubble", 0, true);
	check(predict(threat(w, "stone", 17.0, 20.0), perfect) == "air_spit", "Vacuum Catch");
	// the well
	AgentRef well = Agent::of_move(h.w, a, "vacuum_well", 1, false);
	check(predict(threat(w, "stone", 17.0, 20.0), well) == "capture", "stone x Well T1 (14 / 17 = 0.82): captured");
	check(predict(threat(w, "stone_heavy", 31.5, 45.0), well) == "weaken", "heavy stone: weakened");
	check(predict(threat(w, "boulder", 110.0, 200.0), Agent::of_move(h.w, a, "vacuum_well", 3, false)) == "pass", "boulder: nothing");
	check(predict(threat(w, "magma", 35.0, 20.0), well) == "pass", "magma passes the well");
	// the suction line
	AgentRef suc = Agent::of_move(h.w, a, "vacuum_suction", 0, false);
	const std::string plate = predict(threat(w, "metal", 12.0, 6.0), suc);
	check(plate == "reclaim", "metal <= 12 kg: reclaimed (" + plate + ")");
	check(predict(threat(w, "stone", 17.0, 20.0), suc) == "bend", "a 20 kg stone only bends");
	// the arc
	AgentRef arc = Agent::of_move(h.w, a, "vacuum_arc", 0, false);
	check(predict(threat(w, "flame", 8.0, 0.0, "H"), arc) == "extinguish", "Vacuum Arc: flames out (x2)");
	check(predict(threat(w, "sound", 8.0, 0.0, "P"), arc) == "absorb", "Vacuum Arc silences sound");
}

FF_TEST_F(test_kit_air_vacuum, AVa, test_vacuum_kit_is_deterministic_and_ledgers_balance) {
	std::vector<std::string> hashes;
	for (int k = 0; k < 2; ++k) {
		auto [a, r] = _duel(9.0);
		SimHarness& h = this->h();
		const Snap base = snap(*h.w);
		h.launch_at(a, "stone", 20.0, 10.0, Sim::AMBIENT_C, "", r, 6.0);
		h.aim(a, V3(0, 0, -1));
		h.hold(a, "tech", 50);
		h.step(40);
		run_move(a, "vacuum_palm", 2, 30, 40);
		ledgers_ok(base, "vacuum exchange", 1e-5);
		finite_world("vacuum");
		hashes.push_back(S(a->pos.x, ",", a->pos.y, ",", a->pos.z, "|", r->pos.x, ",", r->pos.z, "|", h.w->bodies.size(), "|", ftos(a->focus, 4), "|",
		                   ftos(r->health, 4)));
	}
	check(hashes[0] == hashes[1], "same seed, same inputs, same state: " + hashes[0] + " vs " + hashes[1]);
}
