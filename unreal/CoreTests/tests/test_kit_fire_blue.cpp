// Port of game/tests/sim/test_kit_fire_blue.gd: Fire / Blue (sub 1): Searing Beam T2 melts a flying 20 kg stone; White
// Core T3 melts through a stone wall (slump ~1 s); Smelter slumps it in ~1 s; Blue Furrow melts the ground into a lava
// channel; Blue Aegis and Corona melt small metal / ice; Kiln burns whoever seizes the superheated body.
#include "ff_test.h"
#include "kit_earth_util.h"
#include "kit_fire_util.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct BlueFx : FireCase {
	static bool is(const Dict& e, const char* k, int v) { return ev_i(e, k, -9999) == v; }
};
using EU::keep;
}  // namespace

FF_TEST_F(test_kit_fire_blue, BlueFx, test_searing_beam_t2_melts_a_flying_stone_into_a_falling_magma_blob) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 10.0);
	SimHarness& h = this->h();
	h.press(p, "attack");
	h.step(56);
	BodyRef st = keep(h.launch_at(p, static_cast<int>(Mat::Stone), 20.0, 17.0, Sim::AMBIENT_C, "", r, 7.0));
	h.w->mass_ledger.ground_taken += 20.0;
	const Snap base = snap(*h.w);
	h.step(8);
	check(p->action != nullptr && p->action->tier() == 2, "charged to T2 (Searing Beam)");
	h.release(p, "attack");
	h.step(3);
	check(st->liquid >= 0.8, S("the stone is molten in flight (liquid ", st->liquid, ")"));
	check(h.has_event("melt_in_flight"), "melt_in_flight event");
	h.step(60);
	ActorState* pp = p;
	check(!h.any_event("hit", [&](const Dict& e) { return is(e, "actor", pp->id); }), "it falls short of the caster");
	check(st->alive && st->is_stone() && (st->pos - p->pos).length() > 1.0f, "a magma blob on the ground in front");
	ledgers_ok(base, "searing beam");
	fx_catalogued("searing beam");
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_white_core_t3_melts_through_a_stone_wall_in_about_one_second) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 9.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef wall = keep(bulwark(r, V3(0, 0, p->pos.z - 3.5), p->facing));
	const Snap base = snap(*h.w);
	h.press(p, "attack");
	h.step(112);
	check(p->action != nullptr && p->action->tier() == 3, "charged to T3 (White Core)");
	h.release(p, "attack");
	const int64_t t0 = h.w->tick;
	h.until([&]() { return h.has_event("slump"); }, 120);
	const double dt = static_cast<double>(h.w->tick - t0) / 60.0;
	check(h.has_event("slump") && !wall->alive, S("the wall slumped (", dt, " s)"));
	check(dt > 0.6 && dt < 1.2, S("in about 1 s (", dt, ")"));
	h.step(60);
	ledgers_ok(base, "white core");
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_smelter_slumps_a_bulwark_in_about_one_second) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 9.0);
	SimHarness& h = this->h();
	r->is_dummy = true;
	BodyRef wall = keep(bulwark(r, V3(0, 0, p->pos.z - 3.0), p->facing));
	const Snap base = snap(*h.w);
	h.press(p, "tech");
	const int64_t t0 = h.w->tick;
	h.until([&]() { return h.has_event("slump"); }, 160);
	const double dt = static_cast<double>(h.w->tick - t0) / 60.0;
	check(h.has_event("slump") && !wall->alive, "the wall slumped");
	check(dt > 0.95 && dt < 1.35, S("in about 1 s incl. the 0.2 s startup (", dt, ")"));
	MatBody* face = h.w->get_body(ev_i(h.last_event("slump"), "body", -1));
	check(face != nullptr && face->alive && face->liquid >= 0.5, "the molten face body exists");
	check(h.has_event("wall_crumble"), "the rest crumbled");
	h.release(p, "tech");
	h.step(30);
	ledgers_ok(base, "smelter");
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_blue_furrow_melts_the_ground_into_a_lava_channel) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 8.0);
	SimHarness& h = this->h();
	const Snap base = snap(*h.w);
	const double taken = h.w->mass_ledger.ground_taken;
	h.flick(p, "attack", static_cast<int>(Gesture::Down));
	h.step(3);
	h.release(p, "attack");
	h.until([&]() { return h.has_event("magma_rift"); }, 40);
	const Dict ev = h.last_event("magma_rift");
	MatBody* b = h.w->get_body(ev_i(ev, "body", -1));
	check(b != nullptr && b->is_stone() && b->form == Form::Wave && b->liquid >= 0.99, "a molten lava channel");
	near(h.w->mass_ledger.ground_taken - taken, 8.0, 1e-6, "its stone came from the ground");
	check(b != nullptr && Interactions::classify(*b) == "lava_wave", "it is a lava wave (Earth / Magma Surge can push it)");
	ActorState* rr = r;
	h.until([&]() { return rr->health < 100.0; }, 90);
	check(r->health < 100.0, "it runs into the rival");
	h.step(60);
	ledgers_ok(base, "blue furrow");
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_blue_aegis_melts_small_metal_and_still_blocks_stones) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 10.0);
	SimHarness& h = this->h();
	h.press(p, "guard");
	h.step(20);
	BodyRef m = keep(h.launch_at(p, static_cast<int>(Mat::Metal), 6.0, 20.0, Sim::AMBIENT_C, "", r, 6.0));
	const Snap base = snap(*h.w);
	const double f0 = p->focus;
	h.step(30);
	check(m->alive && m->liquid > 0.5 && m->mat == Mat::Metal, S("the 6 kg metal is molten (", m->liquid, ")"));
	check(p->health == 100.0, "no damage");
	check(f0 - p->focus > 10.0, S("the Aegis paid the heat (", f0 - p->focus, " Focus incl. upkeep)"));
	ledgers_ok(base, "aegis metal");
	h.launch_at(p, static_cast<int>(Mat::Stone), 20.0, 17.0, Sim::AMBIENT_C, "", r, 6.0);
	h.w->mass_ledger.ground_taken += 20.0;
	h.step(30);
	check(p->health < 100.0 && p->health > 95.0, S("a 20 kg stone is only blocked (chip ", 100.0 - p->health, ")"));
	h.release(p, "guard");
	h.step(10);
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_corona_melts_an_incoming_ice_shard) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 10.0);
	SimHarness& h = this->h();
	h.flick(p, "attack", static_cast<int>(Gesture::Side));
	h.step(3);
	h.release(p, "attack");
	h.until([&]() { return !zones_tagged("corona").empty(); }, 30);
	check(zones_tagged("corona").size() == 1, "a corona around the fighter");
	BodyRef ice = keep(h.launch_at(p, static_cast<int>(Mat::Water), 4.0, 18.0, -5.0, "", r, 5.0));
	const double e1 = ice->thermal_energy();
	ice->liquid = 0.0;
	ice->phase = Phase::Frozen;
	h.w->ledger.freeze_dump += ice->thermal_energy() - e1;
	const Snap base = snap(*h.w);
	h.step(30);
	ActorState* pp = p;
	check(!h.any_event("hit", [&](const Dict& e) { return is(e, "actor", pp->id); }), "the shard never lands");
	check(!ice->alive || ice->phase == Phase::Liquid, "it melted in the ring");
	h.step(60);
	ledgers_ok(base, "corona ice");
}

FF_TEST_F(test_kit_fire_blue, BlueFx, test_kiln_burns_whoever_seizes_the_superheated_stone) {
	auto [p, r] = duel(1, Sim::EARTH, 5, 6.0);
	SimHarness& h = this->h();
	BodyRef st = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, p->pos + p->forward() * 1.6 + V3(0, 0.3, 0), "test"));
	h.w->mass_ledger.ground_taken += 20.0;
	st->on_ground = true;
	const Snap base = snap(*h.w);
	h.press(p, "guard");
	h.step(8);
	h.flick(p, "guard", static_cast<int>(Gesture::Down));
	h.step(40);
	h.release(p, "guard");
	check(h.has_event("kiln") && st->temp >= 600.0, S("the stone is superheated (", st->temp, " C)"));
	h.step(5);
	h.w->take_control(*r, *st, 0.9, "seize");
	h.step(3);
	check(h.has_event("kiln_burn"), "seizing it burns the rival");
	check(r->health < 100.0 && st->controller != r->id, S("hurt (", r->health, ") and the stone dropped"));
	h.step(30);
	ledgers_ok(base, "kiln");
}
