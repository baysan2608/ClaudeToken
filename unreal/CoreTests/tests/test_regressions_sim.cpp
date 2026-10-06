// Port of game/tests/sim/test_regressions_sim.gd: regressions for verified simulation findings: grip requests that outlive
// their action, a seize on a too-heavy stone that ripped one from the ground instead, technique cancels during startup /
// buffer, a stale buffered press eating a guard, held water and walls outliving their guard, pours through thin walls, lava
// at rest in water, remnant lifetime and trim order, guard element switching, attacker Balance below 0, unpaid Earth
// heaves, and the waterskin / steam-cap ledger holes. Each scenario failed before its fix.
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <functional>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
BodyRef keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }

bool in_phase(const ActorState* a, ActionPhase ph) { return a->action != nullptr && a->action->phase == ph; }
bool running(const ActorState* a, const std::string& id) { return a->action != nullptr && a->action->id == id; }

struct RS : HarnessCase {
	std::vector<Dict> _events_for(const std::string& type, const ActorState* a) {
		return h().filter(type, [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id; });
	}
	// Energy identity residual: system_energy() - ledger_balance() must stay constant.
	double _ident() { return h().w->system_energy() - h().w->ledger_balance(); }

	// ---------------------------------------------------------------- technique cancels
	ActorState* _cancel_case(int elem, const Dict& kit, const std::function<void(ActorState*)>& setup, bool same_tick) {
		SimHarness& hh = H(3);
		ActorState* p = hh.actor("P", V3(-8, 0, 8), 0, kit, elem);
		ActorState* o = hh.actor("O", V3(-8, 0, -4), 1, Dict(), Sim::EARTH);
		o->is_dummy = true;
		setup(p);
		hh.step(20);
		hh.log.clear();
		hh.press(p, "tech");
		if (!same_tick) hh.step(3);   // inside every technique's startup
		hh.cancel_tech(p);
		hh.step(90);
		return p;
	}
	void _stone_ahead(ActorState* p) {
		h().w->spawn_body(Mat::Stone, Form::Chunk, 20.0, p->pos + V3(0, 0.3, -3.0), "scenario");
	}
};
}  // namespace

// ================================================================ grips outliving their action

FF_TEST_F(test_regressions_sim, RS, test_earth_tech_tap_with_a_stone_in_reach_leaves_no_orphan_hold) {
	for (int hold : {1, 4, 8}) {
		SimHarness& h = H(1);
		ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
		h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
		BodyRef s = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(1.0, 0.2, 3.0), "scenario"));
		h.step(30);
		const Vec3 rest = s->pos;
		h.press(a, "tech");
		h.step(hold);
		h.release(a, "tech");
		h.step(60);
		check(_events_for("whiff", a).size() == 1, S("hold ", hold, ": the tap whiffs"));
		check(_events_for("control_won", a).empty(), S("hold ", hold, ": no grip is granted after the whiff"));
		check(a->held_body == -1 && s->controller == -1,
		      S("hold ", hold, ": no orphan hold (held ", a->held_body, ", stone ctl ", s->controller, ")"));
		check(s->pos.distance_to(rest) < 0.05, S("hold ", hold, ": the stone stays where it lay (", s->pos, ", was ", rest, ")"));
	}
}

FF_TEST_F(test_regressions_sim, RS, test_magma_grip_on_the_last_window_tick_whiffs_without_holding) {
	SimHarness& h = H(11);
	ActorState* p = h.actor("P", V3(0, 0, 6), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
	ActorState* o = h.actor("O", V3(0, 0, -8), 1, Dict(), Sim::EARTH);
	h.step(30);
	h.press(o, "attack");
	h.step();
	h.release(o, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	BodyRef st = keep(h.w->get_body(ev_i(h.last_event("launch"), "body", -1)));
	if (!check(st != nullptr, "setup: O launched a stone")) return;
	h.step(6);
	h.log.clear();
	h.press(p, "tech");
	bool stale = false;
	for (int k = 0; k < 90; ++k) {
		h.step();
		if (st->controller == p->id && !in_phase(p, ActionPhase::Channel)) stale = true;
	}
	check(_events_for("whiff", p).size() == 1, "setup: the stone reached P's grip range only after the window");
	check(_events_for("control_won", p).empty(), "no grip granted on the whiff tick");
	check(!stale, "P never holds the stone outside the channel");
	check(p->held_body == -1, "P holds nothing");
}

FF_TEST_F(test_regressions_sim, RS, test_grip_is_withdrawn_when_a_later_actor_staggers_the_requester_that_tick) {
	// End to end: B (higher id) flares A on the tick A's seize is queued.
	{
		SimHarness& h = H(1);
		ActorState* a = h.actor("A", V3(0, 0, 0), 0, Dict(), Sim::EARTH);
		ActorState* b = h.actor("B", V3(0, 0, -2.5), 1, Dict(), Sim::FIRE);
		BodyRef s = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(1.5, 0.2, -1.5), "scenario"));
		h.step(30);
		h.press(a, "tech");
		h.step(3);
		h.press(b, "attack");
		h.step();
		h.release(b, "attack");
		bool orphan = false;
		for (int k = 0; k < 60; ++k) {
			h.step();
			if (s->controller == a->id && !running(a, "earth_tech")) orphan = true;
		}
		check(_events_for("stagger", a).size() >= 1, "setup: B's flare staggered A");
		check(!orphan, "A never holds the stone without its technique");
	}
	// White box, same ordering forced: A channels, a grip is queued, then a hit interrupts A.
	for (bool staggered : {false, true}) {
		SimHarness& h = H(1);
		ActorState* c = h.actor("C", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
		h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
		BodyRef behind = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0, 0.2, 8.0), "scenario"));
		h.step(30);
		h.press(c, "tech");
		h.until([&] { return in_phase(c, ActionPhase::Channel); }, 30);
		h.w->request_grip(*c, *behind, 0.9, "seize");
		if (staggered) h.w->_stagger(*c, "light", 0.26, Dict());
		h.w->_resolve_grips();
		if (staggered) {
			check(behind->controller == -1 && c->held_body == -1, S("a staggered requester gets nothing (ctl ", behind->controller, ")"));
		} else {
			check(behind->controller == c->id, "control: an uninterrupted request is granted");
			check(behind->hold_point.distance_to(c->hand_point()) < 1e-4,
			      S("a newly won body homes to the hand, not a stale point (", behind->hold_point, ")"));
		}
	}
}

FF_TEST_F(test_regressions_sim, RS, test_earth_tech_on_a_too_heavy_stone_whiffs_instead_of_ripping) {
	// Held on a boulder, the seize used to report control_fail(mass) and then, past rip_time, quietly rip a 20 kg stone
	// from the ground and hand that over instead.
	for (int hold : {4, 60}) {
		SimHarness& h = H(1);
		ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
		h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
		BodyRef boulder = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 200.0, V3(0, 0.6, 3.0), "scenario"));
		h.step(30);
		h.log.clear();
		h.press(a, "tech");
		bool recovering = false;
		for (int k = 0; k < 90; ++k) {
			if (k == hold) h.release(a, "tech");
			h.step();
			if (!recovering && h.has_event("whiff")) recovering = running(a, "earth_tech") && in_phase(a, ActionPhase::Recovery);
		}
		const std::string label = S("held ", hold, " ticks");
		bool mass_fail = false;
		for (const Dict& e : _events_for("control_fail", a))
			if (ev_s(e, "reason") == "mass" && ev_i(e, "body", -1) == boulder->id) mass_fail = true;
		check(mass_fail, label + ": the boulder is too heavy");
		check(_events_for("whiff", a).size() == 1, S(label, ": the technique whiffs once (", _events_for("whiff", a).size(), ")"));
		check(recovering, label + ": the whiff goes straight to recovery");
		check(_events_for("rip", a).empty() && _events_for("control_won", a).empty(), label + ": no ground stone is ripped instead");
		check(a->held_body == -1 && a->action == nullptr, label + ": nothing held, the technique is over");
	}
	// Control: with nothing in reach, the same hold does rip a stone.
	SimHarness& h = H(1);
	ActorState* c = h.actor("C", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
	h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(30);
	h.press(c, "tech");
	h.step(40);
	check(_events_for("rip", c).size() == 1 && c->held_body >= 0, "control: with no target the held technique rips a stone");
}

// ================================================================ technique cancels

FF_TEST_F(test_regressions_sim, RS, test_technique_cancel_during_startup_or_with_the_press_never_commits) {
	struct Case {
		std::string name;
		int elem;
		Dict kit;
		std::function<void(ActorState*)> setup;
	};
	const std::vector<Case> cases = {
	    {"water", Sim::WATER, Dict(), [](ActorState*) {}},
	    {"fire vent", Sim::FIRE, Dict(), [](ActorState* p) { p->heat_reserve = 120.0; }},
	    {"fire heat", Sim::FIRE, D({{"magma", true}}), [this](ActorState* p) { _stone_ahead(p); }},
	    {"earth", Sim::EARTH, Dict(), [this](ActorState* p) { _stone_ahead(p); }},
	    {"air", Sim::AIR, Dict(), [](ActorState*) {}},
	};
	for (const Case& c : cases) {
		for (bool same_tick : {false, true}) {
			const std::string label = c.name + (same_tick ? " (press+cancel)" : "");
			ActorState* p = _cancel_case(c.elem, c.kit, c.setup, same_tick);
			for (const char* bad : {"launch", "vent", "control_won", "updraft", "rip"})
				check(_events_for(bad, p).empty(), S(label, ": a cancel never commits (", bad, ")"));
			check(p->held_body == -1, label + ": nothing held");
			check(p->grounded, label + ": still on the ground");
			if (same_tick) check(_events_for("action", p).empty(), label + ": a press arriving with its cancel starts nothing");
			else check(_events_for("cancel", p).size() == 1, label + ": the cancel is acknowledged");
		}
	}
}

FF_TEST_F(test_regressions_sim, RS, test_buffered_technique_press_is_dropped_by_a_cancel) {
	for (bool cancel : {false, true}) {
		SimHarness& h = H(6);
		ActorState* p = h.actor("P", V3(-8, 0, 8), 0, Dict(), Sim::WATER);
		ActorState* o = h.actor("O", V3(-8, 0, -4), 1, Dict(), Sim::EARTH);
		o->is_dummy = true;
		h.step(20);
		h.press(p, "attack");
		h.release(p, "attack");
		h.step();
		h.until([&] {
			return in_phase(p, ActionPhase::Recovery) && p->action->t >= dnum(p->action->def, "recovery") - 0.10;
		}, 120);
		h.log.clear();
		h.press(p, "tech");
		h.step();
		check(p->buffered == "tech", "setup: the technique press is buffered during the recovery");
		h.step(2);
		if (cancel) h.cancel_tech(p);
		h.step(40);
		const bool started = h.any_event("action", [&](const Dict& e) { return ev_i(e, "actor", -1) == p->id && ev_s(e, "move") == "water_tech"; });
		if (cancel) {
			check(!started, "the cancelled buffered technique never starts");
			check(_events_for("launch", p).empty(), "nothing is fired");
		} else {
			check(started, "control: without the cancel the buffered technique starts");
		}
	}
}

// ================================================================ buffer vs guard

FF_TEST_F(test_regressions_sim, RS, test_guard_pressed_after_a_buffered_attack_is_not_eaten) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(30);
	h.press(a, "attack");
	h.step(1);
	h.release(a, "attack");
	h.until([&] { return in_phase(a, ActionPhase::Recovery); }, 60);
	h.step(4);
	h.press(a, "attack");   // mashed during the uncancellable part of the recovery: buffered
	h.it(a).attack_held = false;
	h.step(1);
	check(a->buffered == "attack", "setup: the attack is buffered");
	h.until([&] {
		return a->action != nullptr && a->action->t / dnum(a->action->def, "recovery") >= dnum(a->action->def, "cancel");
	}, 30);
	h.press(a, "guard");
	h.step(1);
	check(running(a, "guard"), "the guard starts at the cancel point");
	for (int k = 0; k < 20; ++k) {
		h.step(1);
		if (!running(a, "guard") || !a->guarding) break;
	}
	check(running(a, "guard") && a->guarding, S("the held guard keeps running (", a->action ? a->action->id : std::string("none"), ")"));
}

// ================================================================ guard leftovers

FF_TEST_F(test_regressions_sim, RS, test_water_kept_through_a_non_water_guard_is_dropped) {
	SimHarness& h = H(3);
	ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::WATER);
	h.actor("O", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(5);
	h.press(a, "tech");
	h.step(30);
	BodyRef b = keep(h.w->held(*a));
	if (!check(b != nullptr && b->is_water(), "setup: A holds drawn water")) return;
	h.element(a, Sim::EARTH);
	h.step(1);
	h.press(a, "guard");   // guard cancels the technique; an Earth guard makes no shield
	h.it(a).tech_held = false;
	h.step(10);
	check(b->controller == -1 && a->held_body == -1, S("the Earth guard drops the water (ctl ", b->controller, ")"));
	h.release(a, "guard");
	h.step(20);
	check(a->action == nullptr && a->held_body == -1, "nothing is held after the guard");
}

FF_TEST_F(test_regressions_sim, RS, test_non_earth_guard_lets_the_previous_wall_sink) {
	SimHarness& h = H(3);
	ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
	h.actor("O", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
	h.step(5);
	h.press(a, "guard");
	h.step(15);
	BodyRef wall = keep(h.w->get_body(a->wall_body));
	if (!check(wall != nullptr && wall->wall_rise >= 1.0, "setup: the Earth guard raised a wall")) return;
	h.release(a, "guard");
	h.element(a, Sim::FIRE);
	h.step(7);
	h.press(a, "guard");   // Fire guard (buffered past the guard recovery) while the old wall sinks
	h.step(60);
	check(a->guarding && a->action != nullptr && a->action->element == Sim::FIRE, "setup: the Fire guard is up");
	check(a->wall_body == -1, S("a Fire guard owns no wall (", a->wall_body, ")"));
	check(!wall->alive, S("the old wall sank instead of being maintained (rise ", wall->wall_rise, ")"));
}

FF_TEST_F(test_regressions_sim, RS, test_guard_rules_use_the_element_the_guard_started_with) {
	for (int switch_to : {-1, static_cast<int>(Sim::AIR)}) {
		SimHarness& h = H(1);
		ActorState* g = h.actor("G", V3(0, 0, 6), 0, Dict(), Sim::FIRE);
		ActorState* o = h.actor("O", V3(0, 0, -6), 1, Dict(), Sim::EARTH);
		h.step(30);
		h.press(g, "guard");   // a plain Fire guard
		h.step(30);            // well past the perfect window
		if (switch_to >= 0) h.element(g, switch_to);
		h.press(o, "attack");
		h.step(1);
		h.release(o, "attack");
		h.until([&] { return h.has_event("launch"); }, 60);
		h.until([&] { return h.has_event("block") || h.has_event("deflect") || h.has_event("hit"); }, 90);
		const std::string label = switch_to >= 0 ? "switched to Air" : "no switch";
		check(running(g, "guard") && g->action->element == Sim::FIRE, label + ": setup: the Fire guard is still running");
		check(h.has_event("block") && !h.has_event("deflect"), label + ": the stone is blocked, not air-deflected");
		check(g->health < 100.0, S(label, ": block chip damage applies (", g->health, ")"));
	}
}

FF_TEST_F(test_regressions_sim, RS, test_perfect_guard_that_empties_the_attackers_balance_knocks_them_down) {
	SimHarness& h = H(1);
	ActorState* att = h.actor("ATT", V3(0, 0, 0), 0, Dict(), Sim::FIRE);
	ActorState* d = h.actor("DEF", V3(0, 0, -2.0), 1, Dict(), Sim::FIRE);
	h.step(60);
	att->balance = 10.0;
	att->balance_idle = 0.0;
	h.press(d, "guard");
	h.step(1);
	h.press(att, "attack");
	h.step(1);
	h.release(att, "attack");
	h.step(12);
	check(h.events("perfect_deflect").size() == 1, "setup: the flare met a perfect guard");
	check(att->balance >= 0.0, S("Balance never goes below 0 (", att->balance, ")"));
	check(att->stun_kind == "knockdown", S("Balance 0 knocks the attacker down (stun '", att->stun_kind, "')"));
}

// ================================================================ earth heavy cost

FF_TEST_F(test_regressions_sim, RS, test_earth_heavy_that_cannot_be_paid_is_a_light_shot) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, 4), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, -4), 1, Dict(), Sim::FIRE);
	h.step(60);
	a->focus = 10.0;   // pays the 7 Focus shot, not the 7 more for the heave
	h.press(a, "attack");
	h.step(40);        // held past heavy_min
	h.release(a, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	const Dict launch = h.last_event("launch");
	BodyRef b = keep(h.w->get_body(ev_i(launch, "body", -1)));
	const Dict ea = Moves::defs().get("earth_attack").as_dict();
	bool reported = false;
	for (const Dict& e : _events_for("insufficient", a))
		if (ev_s(e, "move") == "earth_heavy") reported = true;
	check(reported, "the unpaid heave is reported");
	check(!ev_b(launch, "heavy"), "it launches as a light shot");
	if (!check(b != nullptr, "setup: the launched body exists")) return;
	near(b->mass, Sim::STONE_SHOT_MASS, 1e-6, "no extra stone gathered");
	near(b->damage, dnum(ea, "damage"), 1e-6, "light damage");
	near(b->balance_damage, dnum(ea, "balance"), 1e-6, "light balance damage");
	h.until([&] { return !_events_for("hit", t).empty(); }, 120);
	near(t->health, 100.0 - dnum(ea, "damage"), 1e-6, "T takes light damage");
}

// ================================================================ pour vs walls

FF_TEST_F(test_regressions_sim, RS, test_pour_point_blank_into_a_wall_stays_on_this_side) {
	// Arena cover wall (z -1.25..-0.75): P pours south from just north of it.
	{
		SimHarness& h = H(1);
		ActorState* p = h.actor("P", V3(-3.75, 0, -0.2), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		ActorState* e = h.actor("E", V3(-3.75, 0, -6), 1, Dict(), Sim::EARTH);
		BodyRef st = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(-3.75, 0.2, 0.3), "scenario"));
		h.step(25);
		h.aim(p, V3(0, 0, -1));
		h.press(p, "tech");
		if (!check(h.until([&] { return st->phase == Phase::Molten; }, 240) > 0, "setup: the stone melted")) return;
		h.release(p, "tech");
		h.until([&] { return h.has_event("wave_blocked") || st->form == Form::Wave; }, 60);
		h.step(240);
		check(h.has_event("wave_blocked"), "the pour is blocked by the wall");
		check(st->pos.z > -0.75, S("the lava stayed north of the wall (z ", st->pos.z, ")"));
		check(e->health == 100.0 && _events_for("hit", e).empty(), S("E behind the wall is untouched (", e->health, ")"));
	}
	// Earth wall: E raises it 1.25 m in front; P pours into it from just the other side.
	SimHarness& h = H(1);
	ActorState* p = h.actor("P", V3(0, 0, -2.9), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
	ActorState* e = h.actor("E", V3(0, 0, -5.0), 1, Dict(), Sim::EARTH);
	BodyRef st = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0, 0.2, -1.6), "scenario"));
	h.step(25);
	h.press(e, "guard");
	h.aim(p, V3(0, 0, 1));
	h.press(p, "tech");
	if (!check(h.until([&] { return st->phase == Phase::Molten; }, 240) > 0, "setup: the stone melted")) return;
	h.aim(p, V3(0, 0, -1));
	h.step(20);
	BodyRef wall = keep(h.w->get_body(e->wall_body));
	if (!check(wall != nullptr && wall->wall_rise >= 1.0, "setup: E's wall is up")) return;
	h.release(p, "tech");
	h.until([&] { return h.has_event("wave_blocked") || st->form == Form::Wave; }, 60);
	h.step(120);
	const double face = static_cast<double>(wall->pos.z) + static_cast<double>(wall->wall_half.z);
	check(static_cast<double>(st->pos.z) > face, S("the lava stayed on P's side of the earth wall (z ", st->pos.z, ", wall face ", face, ")"));
}

// ================================================================ quench at rest

FF_TEST_F(test_regressions_sim, RS, test_lava_resting_in_the_pool_or_on_a_puddle_quenches) {
	// P melts a stone while standing in the pool, is hit and drops the blob into the water.
	{
		SimHarness& h = H(1);
		ActorState* p = h.actor("P", V3(10, 0, -1), 0, D({{"magma", true}, {"heat_draw", true}}), Sim::FIRE);
		ActorState* o = h.actor("O", V3(10, 0, -9), 1, Dict(), Sim::EARTH);
		BodyRef s = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(10, 0.0, -2.2), "scenario"));
		h.step(30);
		h.aim(p, V3(0, 0, -1));
		h.press(p, "tech");
		if (!check(h.until([&] { return s->phase == Phase::Molten; }, 240) > 0, "setup: the stone melted")) return;
		h.w->hit_actor(*p, D({{"attacker", o->id}, {"attack_id", h.w->new_attack_id()}, {"damage", 5.0}, {"balance", 30.0},
		                      {"kind", "stone"}, {"from", o->pos}}));
		h.log.clear();
		const double pool0 = h.w->pool->mass;
		h.step(30);
		check(s->controller == -1 && h.w->arena.in_pool(static_cast<double>(s->pos.x), static_cast<double>(s->pos.z)),
		      "setup: the blob lies in the pool");
		check(s->phase == Phase::Solid, S("lava in the pool sets within 0.5 s (", s->describe(), ")"));
		check(h.has_event("steam") && h.w->pool->mass < pool0, S("the pool flash-boils (pool ", pool0, " -> ", h.w->pool->mass, " kg)"));
	}
	// A molten blob at rest on a puddle.
	SimHarness& h = H(2);
	BodyRef pd = keep(h.w->spawn_body(Mat::Water, Form::Puddle, 20.0, V3(-2, 0.0, 4.0), "scenario"));
	pd->update_radius_puddle();
	pd->on_ground = true;
	BodyRef blob = keep(h.w->spawn_body(Mat::Stone, Form::Blob, 20.0, V3(-2, 0.06, 4.0), "scenario", Sim::STONE_MELT_C));
	blob->liquid = 1.0;
	blob->phase = Phase::Molten;
	blob->on_ground = true;
	const double e0 = _ident();
	h.step(30);
	check(blob->phase == Phase::Solid, S("lava resting on a puddle sets within 0.5 s (", blob->describe(), ")"));
	check(pd->mass < 20.0 && h.has_event("steam"), S("the puddle boils (", pd->mass, " kg)"));
	near(_ident(), e0, 1e-6, "quenching keeps the energy identity");
}

// ================================================================ lifetime, trim, caps, ledgers

FF_TEST_F(test_regressions_sim, RS, test_reused_old_stone_is_not_removed_mid_flight) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, -4), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, 6), 1, Dict(), Sim::EARTH);
	BodyRef s = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0.8, 0.2, -4.5), "scenario"));
	s->max_life = Sim::REMNANT_LIFETIME;
	h.step(30);
	s->age = Sim::REMNANT_LIFETIME - 0.1;   // a remnant about to decay, lying at A's feet
	h.press(a, "attack");
	h.step(1);
	h.release(a, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	check(ev_i(h.last_event("launch"), "body", -1) == s->id, "setup: A reused the old stone");
	h.until([&] { return !s->alive || !_events_for("hit", t).empty(); }, 120);
	Array despawns;
	for (const Dict& e : h.events("despawn")) despawns.append(e);
	check(s->alive, S("the thrown stone is not removed by lifetime (", despawns, ")"));
	check(_events_for("hit", t).size() == 1, "and it reaches T");
	// A live projectile past its lifetime is never decayed mid-flight.
	BodyRef p2 = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 5.0, V3(8, 3, 8), "scenario"));
	p2->max_life = 1.0;
	p2->age = 5.0;
	p2->attack_id = h.w->new_attack_id();
	p2->attack_owner = a->id;
	p2->vel = V3(0, 2, 0);
	h.step(1);
	check(p2->alive, "an attacking body outlives max_life while it flies");
}

FF_TEST_F(test_regressions_sim, RS, test_trim_decays_the_longest_lying_remnant_not_a_reused_stone) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::EARTH);
	ActorState* t = h.actor("T", V3(0, 0, -6), 1, Dict(), Sim::FIRE);
	// One loose stone at A's feet (lowest id) and five remnants far away: 6 = at the cap.
	BodyRef mine = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0.8, 0.2, 6.5), "scenario"));
	std::vector<BodyRef> others;
	for (int i = 0; i < 5; ++i) others.push_back(keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(-12 + i * 1.2, 0.2, 12), "scenario")));
	h.step(20 * 60);   // all of them lie untouched for 20 s
	h.it(t).move = V3(1, 0, 0);
	h.press(a, "attack");
	h.step(1);
	h.release(a, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	check(ev_i(h.last_event("launch"), "body", -1) == mine->id, "setup: A reused the loose stone");
	h.until([&] { return mine->on_ground && mine->attack_id == 0; }, 120);
	h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(10, 0.2, 12), "scenario");   // a 7th remnant
	h.step(40);
	check(mine->alive, "the stone that just landed survives the trim");
	int dead = 0;
	for (const BodyRef& b : others)
		if (!b->alive) ++dead;
	check(dead == 1, "one long-lying remnant decays instead");
}

FF_TEST_F(test_regressions_sim, RS, test_waterskin_transfers_keep_the_energy_identity) {
	// Shield water drawn from a 0 degC slushy puddle returns to the skin (which holds water at ambient).
	{
		SimHarness& h = H(5);
		ActorState* a = h.actor("D", V3(-4, 0, 5), 0, Dict(), Sim::WATER);
		h.step(5);
		MatBody* puddle = h.w->spawn_body(Mat::Water, Form::Puddle, 4.0, V3(-4, 0, 6.5), "scenario");
		puddle->update_radius_puddle();
		puddle->temp = 0.0;
		puddle->liquid = 0.7;
		a->water_carried = 0.0;
		const double e0 = _ident();
		const double m0 = h.w->water_mass();
		h.press(a, "tech");
		h.step(40);
		MatBody* held = h.w->held(*a);
		if (!check(held != nullptr && held->thermal_energy() < -1.0, "setup: A holds cold slush")) return;
		h.press(a, "guard");
		h.it(a).tech_held = false;
		h.step(10);
		h.release(a, "guard");
		h.step(10);
		check(a->water_carried > 1.0 && a->held_body == -1, S("setup: the shield water went back to the skin (", a->water_carried, " kg)"));
		near(_ident(), e0, 1e-6, "energy identity after the skin takes the water");
		near(h.w->water_mass(), m0, 1e-6, "water mass conserved");
	}
	// Refilling the skin from a warm pool.
	SimHarness& h = H(5);
	ActorState* b = h.actor("B", V3(10, 0, -1), 0, Dict(), Sim::WATER);
	b->water_carried = 0.0;
	h.w->pool->temp = 40.0;
	const double e0 = _ident();
	h.step(5);
	check(b->water_carried >= 6.0 - 1e-6, "setup: the skin refilled in the pool");
	near(_ident(), e0, 1e-6, "energy identity after a refill from a warm pool");
}

FF_TEST_F(test_regressions_sim, RS, test_cap_decaying_a_steam_cloud_records_its_water) {
	SimHarness& h = H(1);
	ActorState* a = h.actor("A", V3(0, 0, 6), 0, Dict(), Sim::FIRE);
	h.step(2);
	CombatWorld& w = *h.w;
	w._spawn_steam(V3(0, 1, 0), 2.0);   // the earliest eligible body
	const double water0 = w.water_mass();
	for (int i = 0; i < 30; ++i) {   // live projectiles fill the cap (never decayed)
		MatBody* s = w.spawn_body(Mat::Stone, Form::Chunk, 5.0, V3(-10 + i * 0.6, 2.0, 0), "scenario");
		s->attack_id = w.new_attack_id();
		s->attack_owner = a->id;
		s->vel = V3(0, 0, 1);
		w.mass_ledger.ground_taken += 5.0;
	}
	w.spawn_body(Mat::Stone, Form::Chunk, 5.0, V3(5, 2, 5), "scenario");
	w.mass_ledger.ground_taken += 5.0;
	bool capped = false;
	for (const Dict& e : w.events)
		if (ev_s(e, "type") == "despawn" && ev_s(e, "reason") == "cap") capped = true;
	check(capped, "setup: the cap decayed a body");
	near(w.water_mass(), water0, 1e-9, "the decayed cloud's water is recorded");
}
