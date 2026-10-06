// Port of game/tests/sim/test_contest.gd: grip contests: two Earth actors fighting over the same loose stone
// (CombatWorld._resolve_grips / take_control / release_body): requests resolved after all actors acted, sorted by (body
// id, strength desc, actor id); exactly one controller per body; losers get control_fail("contest"); an unheld body can be
// taken with any strength > 0.05; a held body only by a challenger that beats the holder's authority by GRIP_MARGIN; a
// thrown body keeps a decaying residual authority that third parties must beat (plus margin) until it expires.
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <cmath>
#include <set>

using namespace ff;
using namespace fft;

namespace {
// earth_tech seizes with base strength 0.85 over its reach (see ActEarth._seek).
constexpr double SEIZE_BASE = 0.85;

struct Ct : HarnessCase {
	ActorState* a = nullptr;
	ActorState* b = nullptr;
	BodyRef stone;

	void _setup(uint64_t seed_value = 1, Vec3 a_pos = V3(0, 0, 1.5), Vec3 b_pos = V3(0, 0, -1.5), double mass = 20.0, Vec3 stone_pos = V3(0, 0.2, 0)) {
		SimHarness& hh = H(seed_value);
		a = hh.actor("A", a_pos, 0, Dict(), Sim::EARTH);
		b = hh.actor("B", b_pos, 1, Dict(), Sim::EARTH);
		stone = hh.w->spawn_body(Mat::Stone, Form::Chunk, mass, stone_pos, "scenario")->shared_from_this();
		hh.step(20);   // the stone settles on the ground, the actors turn to face each other
		hh.log.clear();
	}
	static double _reach() { return dnum(Moves::defs().get("earth_tech").as_dict(), "reach"); }
	bool _unique_ids() {
		std::set<int> seen;
		for (const BodyRef& body : h().w->bodies) {
			if (!body->alive) continue;
			if (seen.count(body->id)) return false;
			seen.insert(body->id);
		}
		return true;
	}
	int _count(const std::string& type, const std::string& key, const Value& value) {
		int n = 0;
		for (const Dict& e : h().events(type))
			if (e.get(key) == value) ++n;
		return n;
	}
	std::string log_str() {
		std::string s;
		for (const Dict& e : h().log) s += Value(e).to_string() + "\n";
		return s;
	}
	std::pair<int, std::string> _run_same_tick(uint64_t seed_value) {
		_setup(seed_value);
		h().press(a, "tech");
		h().press(b, "tech");
		h().step(30);
		return {stone->controller, log_str()};
	}

	struct Steal {
		double z = 0.0, s_a = 0.0, auth = 0.0, s_b = -1.0;
		bool a_got = false, stole = false;
		int fails = 0, lost = 0;
	};
	// A grabs the stone alone from z_a, then B (standing next to A's hand) challenges.
	Steal _steal_trial(double z_a) {
		_setup(1, V3(0, 0, z_a), V3(1.0, 0, z_a - 2.0), 10.0, V3(0, 0.2, 0));
		SimHarness& hh = h();
		Steal out;
		out.z = z_a;
		out.s_a = hh.w->grip_strength(*a, *stone, SEIZE_BASE, _reach());
		hh.press(a, "tech");
		const int got = hh.until([&]() { return stone->controller == a->id; }, 60);
		hh.step(40);   // stone settles at A's hands
		out.a_got = got > 0;
		out.auth = stone->authority;
		if (got <= 0) return out;
		out.s_b = hh.w->grip_strength(*b, *stone, SEIZE_BASE, _reach());
		hh.press(b, "tech");
		hh.step(40);
		out.stole = stone->controller == b->id;
		out.fails = hh.count_events("control_fail", [&](const Dict& e) { return ev_i(e, "actor", -1) == b->id && ev_s(e, "reason") == "contest"; });
		out.lost = static_cast<int>(hh.events("control_lost").size());
		return out;
	}

	struct Residual {
		bool launched = false;
		int64_t launch_tick = 0, won_tick = -1;
		int residual_owner = -1, owner_at_press = -2, owner_after_win = -2, fails_after_launch = 0;
		double residual0 = 0.0, s_c = 0.0, residual_at_press = -1.0, residual_after_win = -1.0;
	};
	// A (Earth) throws a stone with a tap attack while C (third party) is already trying to seize it. The thrown stone is
	// pinned right after the launch so C's grip strength stays constant and only the residual authority changes.
	Residual _residual_world(double c_z, int c_press_after_launch) {
		SimHarness& hh = H(3);
		a = hh.actor("A", V3(0, 0, 3.0), 0, Dict(), Sim::EARTH);
		b = hh.actor("C", V3(0, 0, c_z), 1, Dict(), Sim::EARTH);
		stone = hh.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0.3, 0.2, 2.2), "scenario")->shared_from_this();
		hh.step(20);
		hh.log.clear();
		Residual out;
		if (c_press_after_launch < 0) hh.press(b, "tech");   // C is already contesting while A winds up
		hh.press(a, "attack");
		hh.step();
		hh.release(a, "attack");
		const int t = hh.until([&]() { return hh.has_event("launch"); }, 60);
		out.launched = t > 0;
		if (t <= 0) return out;
		const int64_t launch_tick = hh.last_event("launch").get("tick").as_int();
		// Pin the projectile where it was released.
		stone->vel = Vec3();
		stone->on_ground = true;
		out.launch_tick = launch_tick;
		out.residual_owner = stone->residual_owner;
		out.residual0 = stone->residual_authority;
		out.s_c = hh.w->grip_strength(*b, *stone, SEIZE_BASE, _reach());
		if (c_press_after_launch >= 0) {
			hh.step(c_press_after_launch);
			out.residual_at_press = stone->residual_authority;
			out.owner_at_press = stone->residual_owner;
			hh.press(b, "tech");
		}
		for (int k = 0; k < 120; ++k) {
			hh.step();
			if (out.won_tick < 0 && stone->controller == b->id) {
				out.won_tick = hh.w->tick;
				out.residual_after_win = stone->residual_authority;
				out.owner_after_win = stone->residual_owner;
				break;
			}
		}
		out.fails_after_launch = hh.count_events("control_fail", [&](const Dict& e) {
			return ev_i(e, "actor", -1) == b->id && ev_i(e, "body", -1) == stone->id && ev_s(e, "reason") == "contest" && e.get("tick").as_int() >= launch_tick;
		});
		return out;
	}
};
}  // namespace

// ---------------------------------------------------------------- same-tick contest

FF_TEST_F(test_contest, Ct, test_same_tick_contest_has_exactly_one_controller) {
	_setup();
	SimHarness& h = this->h();
	const int bodies_before = h.w->alive_count();
	const double mass_before = stone->mass;
	h.press(a, "tech");
	h.press(b, "tech");
	h.step(30);
	check(stone->controller == a->id || stone->controller == b->id, S("someone controls the stone (ctl=", stone->controller, ")"));
	const bool a_has = a->held_body == stone->id;
	const bool b_has = b->held_body == stone->id;
	check(a_has != b_has, S("exactly one actor holds the stone (a=", a_has, " b=", b_has, ")"));
	ActorState* winner = a_has ? a : b;
	ActorState* loser = a_has ? b : a;
	check(stone->controller == winner->id, "stone.controller agrees with the holder");
	check(loser->held_body == -1, "loser holds nothing");
	check(_count("control_won", "body", Value(stone->id)) == 1, S("exactly one control_won for the stone, got ", _count("control_won", "body", Value(stone->id))));
	const int sid = stone->id;
	check(h.count_events("control_fail", [&](const Dict& e) { return ev_i(e, "actor", -1) == loser->id && ev_i(e, "body", -1) == sid && ev_s(e, "reason") == "contest"; }) >= 1,
	      "loser got a control_fail(contest) event");
	check(h.count_events("control_fail", [&](const Dict& e) { return ev_i(e, "actor", -1) == winner->id; }) == 0, "winner never got a control_fail");
	check(h.events("control_lost").empty(), "nobody lost control in a fresh contest");
	near(stone->mass, mass_before, 1e-9, "stone mass unchanged by the contest");
	check(h.w->alive_count() == bodies_before, S("no body created or destroyed (", bodies_before, " -> ", h.w->alive_count(), ")"));
	check(_unique_ids(), "no duplicate body ids");
	near(h.w->stone_mass(), mass_before, 1e-9, "stone mass ledger: nothing ripped from the ground");
	check(h.events("rip").empty(), "the loser did not rip a second stone out of the ground");
}

FF_TEST_F(test_contest, Ct, test_equal_strength_tie_goes_to_lowest_actor_id) {
	// Symmetric geometry: both actors are 1.5 m from the stone, so strengths tie exactly.
	_setup();
	SimHarness& h = this->h();
	const double sa = h.w->grip_strength(*a, *stone, SEIZE_BASE, _reach());
	const double sb = h.w->grip_strength(*b, *stone, SEIZE_BASE, _reach());
	near(sa, sb, 1e-4, "symmetric setup gives equal grip strength");
	h.press(b, "tech");   // press order must not matter, only the documented sort
	h.press(a, "tech");
	h.step(30);
	check(stone->controller == a->id, S("tie is broken by the lower actor id (ctl=", stone->controller, ")"));
}

FF_TEST_F(test_contest, Ct, test_loser_never_duplicates_the_stone_while_contesting) {
	_setup();
	SimHarness& h = this->h();
	h.press(a, "tech");
	h.press(b, "tech");
	h.step(150);   // far longer than rip_time: a loser with no target would rip a new stone
	check(h.events("rip").empty(), "no rip events during a long contest");
	check(h.w->alive_count() == 2, S("still pool + one stone (", h.w->alive_count(), " bodies)"));
	near(h.w->mass_ledger.ground_taken, 0.0, 1e-9, "nothing was taken from the ground");
	check(_unique_ids(), "no duplicate body ids");
	check(stone->controller == a->id && b->held_body == -1, "the holder keeps the stone for the whole contest");
}

FF_TEST_F(test_contest, Ct, test_same_tick_winner_is_deterministic) {
	const auto first = _run_same_tick(5);
	for (int k = 0; k < 3; ++k) {
		const auto again = _run_same_tick(5);
		check(again.first == first.first, S("same seed, run ", k, ": same winner"));
		check(again.second == first.second, S("same seed, run ", k, ": identical event log"));
	}
	// Contests use no randomness: other seeds resolve the same way.
	for (uint64_t s : {1u, 7u, 99u, 12345u}) {
		const auto other = _run_same_tick(s);
		check(other.first == first.first, S("seed ", s, ": same winner (", other.first, " vs ", first.first, ")"));
	}
}

FF_TEST_F(test_contest, Ct, test_closer_actor_wins_simultaneous_contest_even_with_higher_id) {
	// A (id 1) is far from the stone, B (id 2) close: strength decides before the id tie-break.
	_setup(1, V3(0, 0, 6.0), V3(0, 0, -1.5));
	SimHarness& h = this->h();
	const double sa = h.w->grip_strength(*a, *stone, SEIZE_BASE, _reach());
	const double sb = h.w->grip_strength(*b, *stone, SEIZE_BASE, _reach());
	check(sb > sa + 0.05, S("setup: B grips clearly stronger (", sb, " vs ", sa, ")"));
	h.press(a, "tech");
	h.press(b, "tech");
	h.step(30);
	check(stone->controller == b->id, S("the stronger (closer) actor wins (ctl=", stone->controller, ")"));
	check(h.any_event("control_fail", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id && ev_s(e, "reason") == "contest"; }), "A got control_fail(contest)");
}

FF_TEST_F(test_contest, Ct, test_mass_limit_boundary_blocks_grip_for_both) {
	// max_control_mass is 80 kg: exactly 80 is allowed, anything above fails for everyone.
	_setup(1, V3(0, 0, 1.5), V3(0, 0, -1.5), 80.0);
	h().press(a, "tech");
	h().step(30);
	check(stone->controller == a->id, "an 80 kg stone (== limit) can be seized");
	_setup(1, V3(0, 0, 1.5), V3(0, 0, -1.5), 80.5);
	SimHarness& h = this->h();
	h.press(a, "tech");
	h.press(b, "tech");
	h.step(30);
	check(stone->controller == -1, "an 80.5 kg stone cannot be seized by anyone");
	const int sid = stone->id;
	const int fails = h.count_events("control_fail", [&](const Dict& e) { return ev_i(e, "body", -1) == sid && ev_s(e, "reason") == "mass"; });
	check(fails >= 2, S("both actors are told the stone is too heavy (", fails, " events)"));
	near(stone->mass, 80.5, 1e-9, "the boulder is untouched");
}

// ---------------------------------------------------------------- hysteresis

FF_TEST_F(test_contest, Ct, test_challenger_must_beat_holder_by_margin) {
	std::vector<Steal> trials;
	for (double z = 2.0; z <= 7.01; z += 0.25) trials.push_back(_steal_trial(z));
	int stolen = 0, within_margin = 0;
	bool last_stole = false, monotonic = true;
	for (const Steal& t : trials) {
		if (!check(t.a_got, S("z=", t.z, ": the lone holder acquires the stone"))) continue;
		near(t.auth, t.s_a, 0.02, S("z=", t.z, ": holder authority equals its grip strength at seize time"));
		const double needed = t.s_a + CombatWorld::GRIP_MARGIN;
		const bool should_steal = t.s_b > needed;
		check(t.stole == should_steal, S("z=", t.z, ": steal=", t.stole, " but challenger ", t.s_b, " vs holder ", t.s_a, " (+margin = ", needed, ")"));
		note(S("z=", t.z, " s_a=", t.s_a, " s_b=", t.s_b, " stole=", t.stole, " fails=", t.fails));
		if (t.stole) {
			++stolen;
		} else if (t.s_b > t.s_a) {
			++within_margin;
			check(t.fails >= 1, S("z=", t.z, ": a challenger inside the margin is told control_fail(contest)"));
			check(t.lost == 0, S("z=", t.z, ": the holder never lost control inside the margin"));
		}
		if (last_stole && !t.stole) monotonic = false;
		last_stole = last_stole || t.stole;
	}
	check(monotonic, "once the holder is weak enough to be stolen from, weaker holders (farther) are too");
	check(stolen >= 1, S("sweep contains a successful steal (", stolen, ")"));
	check(within_margin >= 1, S("sweep contains a stronger-but-inside-margin challenger that fails (", within_margin, ")"));
	check(stolen < static_cast<int>(trials.size()), "sweep contains failed steals");
}

FF_TEST_F(test_contest, Ct, test_holder_losing_control_gets_control_lost_and_clean_state) {
	// A seizes from far away (weak authority); B stands at A's hands and takes it.
	const Steal t = _steal_trial(6.5);
	SimHarness& h = this->h();
	check(t.stole, S("setup: B took the stone (s_b ", t.s_b, " vs s_a ", t.s_a, ")"));
	const int sid = stone->id;
	const std::vector<Dict> lost = h.filter("control_lost", [&](const Dict& e) { return ev_i(e, "actor", -1) == a->id && ev_i(e, "body", -1) == sid; });
	check(lost.size() == 1, S("A receives exactly one control_lost, got ", lost.size()));
	if (!lost.empty()) {
		check(ev_s(lost[0], "reason") == "contest" && ev_i(lost[0], "by", -1) == b->id, "control_lost names the contest and the winner");
		const std::vector<Dict> won = h.filter("control_won", [&](const Dict& e) { return ev_i(e, "actor", -1) == b->id && ev_i(e, "body", -1) == sid; });
		check(won.size() == 1, "B receives exactly one control_won");
		if (!won.empty()) check(ev_i(won[0], "tick") == ev_i(lost[0], "tick"), "loss and win are the same tick");
	}
	check(a->held_body == -1, "A holds nothing after the steal");
	check(b->held_body == stone->id && stone->controller == b->id, "B is the single controller");
	check(h.w->held(*a) == nullptr && h.w->held(*b) == stone.get(), "held() agrees");
	check(_unique_ids() && h.w->alive_count() == 2, "no duplicate bodies");
	// A is still channelling its technique with nothing in hand: releasing must unwind cleanly.
	h.release(a, "tech");
	const int done = h.until([&]() { return a->action == nullptr; }, 90);
	check(done > 0, "A's action finishes after releasing (no stuck channel)");
	check(a->stun == 0.0 && !a->guarding && a->held_body == -1, S("A is fully idle: stun ", a->stun, " guard ", a->guarding, " held ", a->held_body));
	check(stone->controller == b->id, "A's cleanup did not touch B's stone");
	// B can still use the stolen stone normally.
	h.aim(b, V3(0, 0, 1));
	h.release(b, "tech");
	const int launched = h.until([&]() { return h.has_event("launch"); }, 60);
	check(launched > 0, "B throws the stolen stone");
	check(stone->attack_owner == b->id, "stone is B's attack");
}

// ---------------------------------------------------------------- residual authority

FF_TEST_F(test_contest, Ct, test_thrown_stone_residual_authority_delays_third_party_regrab) {
	// C is 5.65 m from the pinned stone: strong enough to take an unheld stone, too weak to beat the thrower's residual
	// authority plus margin until it has decayed.
	const Residual r = _residual_world(-3.5, -1);
	check(r.launched, "A launched the stone");
	if (!r.launched) return;
	check(r.residual_owner == a->id, "thrown stone remembers its thrower as residual owner");
	check(r.residual0 > CombatWorld::RESIDUAL_START - 0.1 && r.residual0 <= CombatWorld::RESIDUAL_START, S("residual starts near RESIDUAL_START (", r.residual0, ")"));
	const double s_c = r.s_c;
	check(s_c > 0.05 && s_c + 0.001 < CombatWorld::RESIDUAL_START + CombatWorld::GRIP_MARGIN,
	      S("setup: C is strong enough alone but weaker than residual+margin (", s_c, ")"));
	check(r.fails_after_launch >= 1, S("C's immediate attempts fail with control_fail(contest) (", r.fails_after_launch, ")"));
	check(r.won_tick > 0, "C does get the stone once the residual has decayed");
	// Earliest tick C can win: residual must have decayed below strength - margin.
	const double need_decay = CombatWorld::RESIDUAL_START - (s_c - CombatWorld::GRIP_MARGIN);
	const int min_ticks = static_cast<int>(std::floor(need_decay / CombatWorld::RESIDUAL_DECAY / Sim::DT)) - 2;
	const int waited = static_cast<int>(r.won_tick - r.launch_tick);
	note(S("s_c ", s_c, " residual0 ", r.residual0, " waited ", waited, " ticks (min ", min_ticks, "), fails ", r.fails_after_launch));
	check(waited >= min_ticks, S("C had to wait for the decay: ", waited, " ticks (need >= ~", min_ticks, ")"));
	check(waited <= static_cast<int>(CombatWorld::RESIDUAL_START / CombatWorld::RESIDUAL_DECAY / Sim::DT) + 3, S("C wins no later than full decay (", waited, " ticks)"));
	check(r.owner_after_win == -1 && r.residual_after_win == 0.0, "taking control clears the residual");
}

FF_TEST_F(test_contest, Ct, test_regrab_after_residual_decay_is_immediate) {
	// Same geometry, but C only starts well after the residual has expired.
	const int ticks_to_zero = static_cast<int>(std::ceil(CombatWorld::RESIDUAL_START / CombatWorld::RESIDUAL_DECAY / Sim::DT)) + 2;
	const Residual r = _residual_world(-3.5, ticks_to_zero);
	check(r.launched, "A launched the stone");
	check(r.owner_at_press == -1 && r.residual_at_press == 0.0,
	      S("residual fully decayed after ", ticks_to_zero, " ticks (owner ", r.owner_at_press, ", ", r.residual_at_press, ")"));
	check(r.won_tick > 0, "C takes the stone");
	check(r.fails_after_launch == 0, S("no contest failures once the residual is gone (", r.fails_after_launch, ")"));
}

FF_TEST_F(test_contest, Ct, test_close_third_party_beats_residual_authority) {
	// A challenger standing at the thrower's hands is stronger than residual + margin even at once.
	const Residual r = _residual_world(1.2, -1);
	check(r.launched, "A launched the stone");
	check(r.s_c > CombatWorld::RESIDUAL_START + CombatWorld::GRIP_MARGIN - 0.05, S("setup: C is near the stone (", r.s_c, ")"));
	check(r.won_tick > 0, "a close, strong third party can take a just-thrown stone");
	if (r.won_tick > 0) check(r.won_tick - r.launch_tick <= 8, S("and does so within a few ticks (", r.won_tick - r.launch_tick, ")"));
}
