// Port of game/tests/sim/test_core_input.gd: input to move (sub-element select, gesture slots and morphs, guard
// push/sink, evade_hold, chains) with test-local moves (COMBAT_SPEC "Engine" §E2).
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

using namespace ff;
using namespace fft;

namespace {
struct InputFx : HarnessCase {
	ActorState* p = nullptr;
	ActorState* o = nullptr;
	SimHarness& _setup(int elem = Sim::EARTH) {
		SimHarness& h = H(1);
		h.begin_scope();
		h.legacy_bindings_only();   // engine mechanics with test-local moves
		Moves::register_def("t_strike", D({{"element", 0}, {"verb", "projectile"}, {"slot", "strike"}, {"startup", 0.24}, {"active", 0.06},
		                                   {"recovery", 0.3}, {"cancel", 0.6}, {"heavy_min", 0.4}, {"cost", 5.0}, {"source", "ground"}, {"mat", "stone"},
		                                   {"mass", 10.0}, {"speed", 18.0},
		                                   {"tiers", D({{"t1", D({{"mass", 16.0}})}, {"t2", D({{"mass", 22.0}})}, {"t3", D({{"mass", 30.0}})}})}}));
		Moves::register_def("t_thrust", D({{"element", 0}, {"verb", "projectile"}, {"slot", "thrust"}, {"startup", 0.2}, {"active", 0.05},
		                                   {"recovery", 0.3}, {"cost", 6.0}, {"source", "ground"}, {"mat", "stone"}, {"mass", 6.0}, {"speed", 26.0},
		                                   {"gravity", 0.4}, {"tag", "spear"}, {"tiers", D({{"t1", D({{"count", 2}})}, {"t2", D({{"count", 3}})}})}}));
		Moves::register_def("t_wall", D({{"element", 0}, {"verb", "barrier"}, {"barrier", "wall"}, {"mat", "sand"}, {"tag", "sand"}, {"mass", 100.0},
		                                 {"cost", 7.0}, {"tiers", D({{"t1", D({{"mass", 130.0}})}, {"t2", D({{"mass", 160.0}})}})},
		                                 {"counter", D({{"cls", "wall_sand"}})}}));
		Moves::register_def("t_push", D({{"element", 0}, {"verb", "burst"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"at", "ahead"},
		                                 {"distance", 2.0}, {"radius", 1.5}, {"power", 10.0}}));
		Moves::register_def("t_sink", D({{"element", 0}, {"verb", "zone"}, {"startup", 0.1}, {"active", 0.05}, {"recovery", 0.2}, {"tag", "quicksand"},
		                                 {"radius", 3.0}, {"life", 2.0}}));
		Moves::register_def("t_glide", D({{"element", 0}, {"verb", "mode"}, {"kind", "skate"}, {"speed_mult", 1.3}, {"upkeep", 4.0}, {"startup", 0.0},
		                                  {"active", 0.0}, {"recovery", 0.1}}));
		Moves::bind(0, 1, "strike", "t_strike");
		Moves::bind(0, 1, "thrust", "t_thrust");
		Moves::bind(0, 1, "guard", "t_wall");
		Moves::bind(0, 1, "push", "t_push");
		Moves::bind(0, 1, "sink", "t_sink");
		Moves::bind(0, 1, "evade_hold", "t_glide");
		p = h.actor("P", Vec3(0, 0, 4), 0, Dict(), elem);
		o = h.actor("O", Vec3(0, 0, -6), 1, Dict(), Sim::FIRE);
		o->is_dummy = true;
		h.step(10);
		h.log.clear();
		return h;
	}
	void _done() { h().end_scope(); }
	void reg_jab() {
		Moves::register_def("t_jab", D({{"element", 0}, {"verb", "cone"}, {"startup", 0.05}, {"active", 0.05}, {"recovery", 0.4}, {"cancel", 0.8},
		                                {"chain", 0.25}, {"range", 12.0}, {"angle", 30.0}, {"damage", 2.0}, {"balance", 2.0}, {"cls", "gust"}, {"power", 5.0}}));
		Moves::bind(0, 1, "strike", "t_jab");
	}
	std::string act_id() const { return p->action ? p->action->id : std::string("none"); }
};
const int kUp = static_cast<int>(Gesture::Up);
const int kDown = static_cast<int>(Gesture::Down);
}  // namespace

FF_TEST_F(test_core_input, InputFx, test_sub_select_emits_and_affects_the_next_action_only) {
	SimHarness& h = _setup();
	h.press(p, "attack");
	h.step(2);
	check(p->action != nullptr && p->action->id == "earth_attack" && p->action->sub == 0, "sub 0: legacy strike");
	h.sub(p, 1);
	h.step();
	check(p->sub() == 1, "sub switched");
	const Dict ev = h.last_event("element");
	check(ev.get("sub", -1) == 1 && ev.get("element", -1) == 0, S("element event carries the sub (", ev, ")"));
	check(p->action != nullptr && p->action->id == "earth_attack" && p->action->sub == 0, "the running action keeps its sub");
	h.release(p, "attack");
	h.until([&] { return p->action == nullptr; }, 120);
	h.step(30);
	h.press(p, "attack");
	h.step(1);
	check(p->action != nullptr && p->action->id == "t_strike" && p->action->sub == 1 && p->action->slot == "strike", "the next action uses sub 1");
	auto act = h.filter("action", [](const Dict& e) { return e.get("move") == "t_strike"; });
	check(!act.empty() && act[0].get("slot", "") == "strike" && act[0].get("sub", -1) == 1 && act[0].has("tier"), "action events gain sub, slot, tier");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_press_with_gesture_starts_the_gesture_move) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	h.flick(p, "attack", kUp);
	h.step();
	check(p->action != nullptr && p->action->id == "t_thrust" && p->action->slot == "thrust", "desktop: press + flick on one tick = thrust (" + act_id() + ")");
	h.release(p, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	MatBody* b = h.w->get_body(ev_i(h.last_event("launch"), "body", -1));
	check(b != nullptr && b->tag == "spear" && is_equal_approx(b->gravity_scale, 0.4), "the spear flies flat");
	check(b != nullptr && b->attack_owner == p->id && b->tier == 0, "an attack of P at T0");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_unbound_gesture_falls_back_to_the_strike) {
	SimHarness& h = _setup();
	h.flick(p, "attack", kUp);   // sub 0: no thrust bound in the legacy kit
	h.step();
	check(p->action != nullptr && p->action->id == "earth_attack" && p->action->slot == "strike", "legacy strike");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_gesture_morph_inside_the_window_keeps_time_and_cost) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	const double f0 = p->focus;
	h.press(p, "attack");
	h.step(4);
	check(p->action != nullptr && p->action->id == "t_strike", "strike started");
	const double total = p->action ? p->action->total : 0.0;
	h.flick(p, "attack", kUp, false);
	h.step();
	check(p->action != nullptr && p->action->id == "t_thrust", "morphed into the thrust");
	check(h.has_event("morph", "to", "t_thrust"), "morph event");
	check(p->action != nullptr && p->action->total >= total, "elapsed time carried");
	near(f0 - p->focus, 6.0, 1e-6, "paid Focus carried: 5 paid for the strike + 1 more for the 6-Focus thrust");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_gesture_after_the_window_does_not_morph) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	h.press(p, "attack");
	h.step(9);
	h.flick(p, "attack", kUp, false);
	h.step();
	check(p->action != nullptr && p->action->id == "t_strike", "too late: still the strike");
	check(!h.has_event("morph"), "no morph");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_gesture_at_charge_release_morphs_with_the_tier) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	h.press(p, "attack");
	h.step(64);
	check(p->action != nullptr && p->action->phase == ActionPhase::Charge && p->action->tier() == 2, S("charging at T2 (", p->action ? p->action->tier() : -1, ")"));
	h.release(p, "attack");
	h.flick(p, "attack", kUp, false);
	h.step();
	check(h.has_event("morph", "at", "release"), "morph at release");
	h.step(3);
	const int pid = p->id;
	auto launches = h.filter("launch", [&](const Dict& e) { return e.get("actor") == pid; });
	check(launches.size() == 3, S("the thrust fired at the carried T2: 3 spears (", launches.size(), ")"));
	for (const Dict& e : launches) {
		MatBody* b = h.w->get_body(ev_i(e, "body"));
		check(b != nullptr && b->tier == 2, "spear carries tier 2");
	}
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_guard_spec_barrier_then_push_and_sink) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	h.press(p, "guard");
	h.step(3);
	check(p->action != nullptr && p->action->id == "guard" && p->action->data.get("spec", "") == "t_wall", "the action stays 'guard' with the spec");
	MatBody* wall = h.w->get_body(p->wall_body);
	check(wall != nullptr && wall->mat == Mat::Sand && wall->tag == "sand", "the spec raised a sand wall");
	check(wall != nullptr && Interactions::counter_class(*wall, h.w) == "wall_sand", "counter class from the spec");
	h.step(62);
	check(wall != nullptr && is_equal_approx(wall->mass, 160.0), S("held to T2: 160 kg (", wall ? wall->mass : 0.0, ")"));
	h.flick(p, "guard", kUp);
	h.step();
	check(p->action != nullptr && p->action->id == "t_push" && dbool(p->action->data, "from_guard", false), "guard flick up = push");
	check(p->action != nullptr && wall != nullptr && p->action->tier() == 2 && dint(p->action->data, "keep_wall") == wall->id, "push keeps the guard's tier and wall");
	h.step(5);
	check(wall != nullptr && wall->alive && wall->wall_rise > 0.9, "the wall is still maintained during the push");
	h.until([&] { return p->action == nullptr; }, 60);
	// Sink
	h.release(p, "guard");
	h.step(30);
	h.press(p, "guard");
	h.step(3);
	h.flick(p, "guard", kDown);
	h.step(10);
	check(h.any_event("zone", [](const Dict& e) { return e.get("kind") == "quicksand" && e.get("phase") == "open"; }), "guard flick down = sink (zone opened)");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_attack_press_still_cancels_a_guard_into_an_attack) {
	SimHarness& h = _setup();
	h.press(p, "guard");
	h.step(10);
	h.press(p, "attack");
	h.step();
	check(p->action != nullptr && p->action->id == "earth_attack", "legacy: attack during guard = attack");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_evade_hold_morphs_after_0_2_s_when_bound) {
	SimHarness& h = _setup();
	h.sub(p, 1);
	h.step();
	h.evade_hold(p, 6);
	check(!h.has_event("morph"), "a short evade stays an evade");
	h.until([&] { return p->action == nullptr; }, 60);
	h.step(30);
	h.press(p, "evade");
	h.it(p).evade_held = true;
	h.step(14);
	check(p->action != nullptr && p->action->id == "t_glide" && p->action->slot == "evade_hold", "held 0.23 s: evade_hold mode");
	check(p->stance == "skate", "mode stance on");
	h.it(p).evade_held = false;
	h.step(10);
	check(p->stance.empty() && (p->action == nullptr || p->action->id != "t_glide" || p->action->phase == ActionPhase::Recovery), "released: mode ends");
	// sub 0: nothing bound
	h.sub(p, 0);
	h.step(30);
	h.press(p, "evade");
	h.it(p).evade_held = true;
	h.step(14);
	check(p->action == nullptr || p->action->id == "evade", "legacy evade is not morphed");
	h.it(p).evade_held = false;
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_chain_window_after_contact) {
	SimHarness& h = _setup();
	reg_jab();
	h.sub(p, 1);
	h.step();
	o->is_dummy = true;
	h.press(p, "attack");
	h.release(p, "attack");
	h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 30);
	check(p->action != nullptr && dbool(p->action->data, "contact", false), "the jab made contact");
	h.step(static_cast<int>(0.4 * 0.3 * 60));
	h.flick(p, "attack", kUp);
	h.step();
	check(p->action != nullptr && p->action->id == "t_thrust", "chained into the thrust");
	check(h.has_event("chain"), "chain event");
	check(dint(p->chain, "n") == 2 && arr_has_str(darr(p->chain, "slots"), "thrust"), "string bookkeeping");
	_done();
}

FF_TEST_F(test_core_input, InputFx, test_weave_switches_sub_element_inside_a_chain_for_6_focus) {
	SimHarness& h = _setup();
	reg_jab();
	Moves::bind(0, 2, "thrust", "t_thrust");
	h.sub(p, 1);
	h.step();
	h.press(p, "attack");
	h.release(p, "attack");
	h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 30);
	h.sub(p, 2);
	h.step(static_cast<int>(0.4 * 0.3 * 60));
	const double f = p->focus;
	h.flick(p, "attack", kUp);
	h.step();
	check(p->action != nullptr && p->action->id == "t_thrust" && p->action->sub == 2, "woven into sub 2's thrust");
	check(h.has_event("weave"), "weave event");
	near(f - p->focus, 6.0 + 6.0, 1e-6, "weave 6 + thrust 6 Focus");
	check(dbool(p->chain, "weaved"), "once per string");
	_done();
}
