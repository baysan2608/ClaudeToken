// Port of game/tests/sim/test_combat_rules.gd: core combat rules: hit deduplication, evade i-frames, guard facing, perfect
// Earth guard, guard mashing, element switching, the input buffer, recovery cancels, knockdown/getup and Focus bounds.
// Scenarios use real inputs through SimHarness; stones that must arrive at an exact moment are spawned as projectiles with
// a gravity-compensated launch (ActEarth::launch_vel).
#include "ff_test.h"
#include "sim_harness.h"

#include "Combat/Acts.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"
#include "Util/Rng.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
BodyRef keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }

std::string f2(double v, int prec) {
	char b[64];
	std::snprintf(b, sizeof(b), "%.*f", prec, v);
	return b;
}

double earth_attack(const char* key) { return dnum(Moves::defs().get("earth_attack").as_dict(), key); }

struct GuardRow {
	double health = 0.0, balance = 0.0;
	size_t block = 0, hit = 0, perfect = 0;
};

struct EarthRow {
	BodyRef stone;
	int redirects = 0;
	size_t p_hits = 0, o_hits = 0;
	int walls_blocked = 0;
	size_t crumbles = 0;
	double closest = 99.0;
};

struct CR : HarnessCase {
	ActorState* p = nullptr;
	ActorState* o = nullptr;

	void _duel(int p_elem = Sim::FIRE, int o_elem = Sim::EARTH, Vec3 p_pos = V3(0, 0, 6), Vec3 o_pos = V3(0, 0, -6), uint64_t seed_value = 2) {
		SimHarness& hh = H(seed_value);
		p = hh.actor("P", p_pos, 0, Dict(), p_elem);
		o = hh.actor("O", o_pos, 1, Dict(), o_elem);
		hh.step(25);   // actors turn to face each other
		hh.log.clear();
	}
	BodyRef _stone(Vec3 from, Vec3 to, ActorState* owner, double speed = 17.0, double mass = 20.0) {
		BodyRef b = keep(h().w->spawn_body(Mat::Stone, Form::Chunk, mass, from, "scenario"));
		b->vel = ActEarth::launch_vel(from, to, speed);
		b->attack_id = h().w->new_attack_id();
		b->attack_owner = owner->id;
		b->damage = 12.0;
		b->balance_damage = 24.0;
		b->hit_set.add(owner->id);
		return b;
	}
	std::vector<Dict> _events_for(const std::string& type, const ActorState* actor) {
		return h().filter(type, [&](const Dict& e) { return ev_i(e, "actor", -1) == actor->id; });
	}
	// hit_actor()/spend_focus() called directly queue events on the world: move them into the harness log.
	void _flush() {
		for (Dict& e : h().w->take_events()) h().log.push_back(e);
	}
	Dict _hit_info(const ActorState* attacker, double damage, double balance, const std::string& kind = "stone") {
		return D({{"attacker", attacker->id}, {"attack_id", h().w->new_attack_id()}, {"damage", damage}, {"balance", balance},
		          {"kind", kind}, {"from", attacker->pos}});
	}
	std::string _hit(ActorState* t, const Dict& info) { return h().w->hit_actor(*t, info); }

	// Outcome of an evade pressed `delay` ticks after a stone was thrown at P: "evaded", "hit" or "none".
	std::string _evade_trial(int delay, bool toward) {
		_duel();
		SimHarness& hh = h();
		hh.press(o, "attack");
		hh.step();
		hh.release(o, "attack");
		hh.until([&] { return hh.has_event("launch"); }, 60);
		hh.step(delay);
		if (toward) hh.it(p).move = V3(0, 0, -1);
		hh.press(p, "evade");
		hh.step();
		hh.it(p).move = Vec3();
		hh.step(100);
		const size_t ev = _events_for("evaded", p).size();
		const size_t hit = _events_for("hit", p).size();
		if (hit > 0) {
			check(ev == 0, S("delay ", delay, ": a stone that hits was not also evaded"));
			check(hit == 1, S("delay ", delay, ": hit exactly once (", hit, ")"));
			return "hit";
		}
		if (ev > 0) {
			check(ev == 1, S("delay ", delay, ": exactly one evaded event (", ev, ")"));
			check(p->health == 100.0, S("delay ", delay, ": no damage after evading"));
			return "evaded";
		}
		return "none";
	}

	// P (Fire: no wall or shield) faces -z towards O. A stone arrives from `angle_deg` around P (0 = from the front,
	// 180 = from behind), 4 m away, ~14 ticks of flight. The guard is pressed `guard_after_spawn` ticks after the stone
	// appears (negative = that many ticks before).
	GuardRow _guard_trial(double angle_deg, bool guard, int guard_after_spawn = -40) {
		_duel(Sim::FIRE, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -14));
		SimHarness& hh = h();
		const double a = deg_to_rad(angle_deg);
		const Vec3 from = V3(std::sin(a) * 4.0, 1.25, -std::cos(a) * 4.0);
		if (guard && guard_after_spawn < 0) {
			hh.press(p, "guard");
			hh.step(-guard_after_spawn);
		}
		_stone(from, p->chest(), o);
		if (guard && guard_after_spawn >= 0) {
			hh.step(guard_after_spawn);
			hh.press(p, "guard");
		}
		hh.step(40);
		GuardRow r;
		r.health = p->health;
		r.balance = p->balance;
		r.block = _events_for("block", p).size();
		r.hit = _events_for("hit", p).size();
		r.perfect = hh.events("perfect_deflect").size();
		return r;
	}

	// P (Earth) presses guard; a stone is launched from `dist` m away at the same tick. O (the thrower) stands `o_dist` m
	// behind the stone's origin line.
	EarthRow _earth_trial(double dist, double o_dist = 12.0, bool mash = false) {
		_duel(Sim::EARTH, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -o_dist));
		SimHarness& hh = h();
		BodyRef s;
		if (mash) {
			hh.press(p, "guard");
			hh.step();
			hh.release(p, "guard");
			hh.step(2);
			hh.press(p, "guard");   // buffered behind the first guard's recovery: a mashed second guard
			s = _stone(V3(0, 1.25, -dist), p->chest(), o);
			hh.step();
		} else {
			hh.press(p, "guard");
			s = _stone(V3(0, 1.25, -dist), p->chest(), o);
			hh.step();
		}
		double closest = 99.0;
		for (int k = 0; k < 120; ++k) {
			hh.step();
			if (s->alive && s->attack_owner == p->id) closest = minf(closest, static_cast<double>(s->pos.distance_to(o->chest())));
		}
		EarthRow r;
		r.stone = s;
		r.redirects = hh.count_events("perfect_deflect", [](const Dict& e) { return ev_s(e, "verb") == "redirect"; });
		r.p_hits = _events_for("hit", p).size();
		r.o_hits = _events_for("hit", o).size();
		r.walls_blocked = hh.count_events("block", [](const Dict& e) { return ev_s(e, "kind") == "wall"; });
		r.crumbles = hh.events("wall_crumble").size();
		r.closest = closest;
		return r;
	}

	void _tap_attack() {
		h().press(p, "attack");
		h().step();
		h().release(p, "attack");
	}
	std::vector<Dict> _actions(const std::function<bool(const Dict&)>& pred) { return h().filter("action", pred); }

	// Starts a second tap attack `k` ticks before the first one's recovery finishes; returns the tick at which the second
	// action started, or -1 if it never did.
	int64_t _buffer_trial(int k, int64_t finish_tick) {
		_duel(Sim::EARTH);
		_tap_attack();
		SimHarness& hh = h();
		while (hh.w->tick < finish_tick - k) hh.step();
		hh.press(p, "attack");
		hh.step();
		hh.release(p, "attack");
		hh.step(90);
		const std::vector<Dict> starts = _actions([&](const Dict& e) {
			return ev_i(e, "actor", -1) == p->id && ev_s(e, "move") == "earth_attack" && ev_s(e, "phase") == "startup";
		});
		if (starts.size() < 2) return -1;
		return ev(starts[1], "tick").as_int();
	}
	int64_t _last_done_tick() {
		const std::vector<Dict> done = _actions([&](const Dict& e) { return ev_i(e, "actor", -1) == p->id && ev_s(e, "phase") == "done"; });
		return done.empty() ? -1 : ev(done.back(), "tick").as_int();
	}
};
}  // namespace

// ================================================================ hit deduplication

FF_TEST_F(test_combat_rules, CR, test_hit_actor_deduplicates_by_attack_id) {
	_duel();
	SimHarness& h = this->h();
	const Dict info = _hit_info(o, 10.0, 5.0);
	check(_hit(p, info) == "hit", "first application hits");
	near(p->health, 90.0, 1e-9, "health after one hit");
	for (int k = 0; k < 5; ++k) check(_hit(p, info) == "dup", S("repeat ", k, " with the same attack id is a dup"));
	near(p->health, 90.0, 1e-9, "no extra damage from repeats");
	check(_hit(p, _hit_info(o, 10.0, 5.0)) == "hit", "a new attack id hits again");
	near(p->health, 80.0, 1e-9, "second attack applied");
	// Evaded attacks are deduplicated too: one 'evaded' event, then silence.
	p->iframes = 1.0;
	const Dict evd = _hit_info(o, 10.0, 5.0);
	check(_hit(p, evd) == "evaded", "i-frames evade");
	check(_hit(p, evd) == "dup", "the same evaded attack never re-triggers");
	_flush();
	check(h.events("evaded").size() == 1, S("one evaded event, got ", h.events("evaded").size()));
}

FF_TEST_F(test_combat_rules, CR, test_fast_stone_overlapping_an_evading_actor_for_several_ticks_reports_once) {
	_duel(Sim::FIRE, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -14));
	SimHarness& h = this->h();
	p->iframes = 10.0;
	BodyRef s = _stone(V3(0, 1.25, -6), p->chest(), o);
	int overlap_ticks = 0;
	for (int k = 0; k < 60; ++k) {
		h.step();
		if (s->alive && h.w->_touches_actor(*s, *p, 0.0)) overlap_ticks += 1;
	}
	check(overlap_ticks >= 2, S("setup: the stone overlapped P for ", overlap_ticks, " ticks"));
	check(_events_for("evaded", p).size() == 1, S("exactly one evaded event, got ", _events_for("evaded", p).size()));
	check(_events_for("hit", p).empty(), "no hit");
	near(p->health, 100.0, 1e-9, "no damage");
}

FF_TEST_F(test_combat_rules, CR, test_stone_overlapping_a_target_for_several_ticks_hits_once) {
	_duel(Sim::FIRE, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -14));
	SimHarness& h = this->h();
	BodyRef s = _stone(V3(0, 1.25, -6), p->chest(), o);
	int overlap_ticks = 0;
	for (int k = 0; k < 90; ++k) {
		h.step();
		if (s->alive && h.w->_touches_actor(*s, *p, 0.0)) overlap_ticks += 1;
	}
	check(overlap_ticks >= 2, S("setup: the stone overlapped P for ", overlap_ticks, " ticks"));
	check(_events_for("hit", p).size() == 1, S("exactly one hit, got ", _events_for("hit", p).size()));
	near(p->health, 100.0 - 12.0, 1e-9, S("damage applied once (", f2(p->health, 1), ")"));
	check(s->attack_id == 0, "the stone is inert after hitting");
}

// ================================================================ evade i-frames

FF_TEST_F(test_combat_rules, CR, test_evade_iframes_make_a_timed_stone_pass) {
	for (bool toward : {false, true}) {
		std::vector<std::string> outcomes;
		for (int d = 0; d < 56; ++d) outcomes.push_back(_evade_trial(d, toward));
		int first = -1, last = -1;
		for (int k = 0; k < static_cast<int>(outcomes.size()); ++k) {
			if (outcomes[static_cast<size_t>(k)] == "evaded") {
				if (first < 0) first = k;
				last = k;
			}
		}
		const std::string label = toward ? "dash toward" : "backstep";
		check(first >= 0, label + ": some timing evades the stone");
		if (first < 0) continue;
		for (int k = first; k <= last; ++k)
			check(outcomes[static_cast<size_t>(k)] == "evaded",
			      S(label, ": the evade window is contiguous (delay ", k, " is ", outcomes[static_cast<size_t>(k)], ")"));
		check(outcomes.front() == "hit", label + ": evading far too early gets hit");
		check(outcomes.back() == "hit", label + ": evading too late gets hit");
		const int width = last - first + 1;
		note(S(label, ": stone evaded for press delays ", first, "..", last, " (", width, " ticks)"));
		check(width >= (toward ? 8 : 2), S(label, ": the window covers the i-frames (", width, " ticks)"));
		check(width <= 30, S(label, ": i-frames do not make a huge window (", width, ")"));
		check(std::find(outcomes.begin(), outcomes.end(), "none") == outcomes.end(), label + ": every timing resolves to evaded or hit");
	}
}

FF_TEST_F(test_combat_rules, CR, test_evade_iframes_last_the_documented_duration) {
	_duel();
	SimHarness& h = this->h();
	check(p->iframes == 0.0, "no i-frames before evading");
	h.press(p, "evade");
	h.step();
	const double frames = dnum(Moves::defs().get("evade").as_dict(), "iframes");
	check(p->iframes > 0.0 && p->iframes <= frames, S("i-frames start with the evade (", f2(p->iframes, 3), ")"));
	int ticks = 1;
	while (p->iframes > 0.0 && ticks < 60) {
		h.step();
		ticks += 1;
	}
	check(std::fabs(static_cast<double>(ticks) * Sim::DT - frames) <= 2.0 * Sim::DT, S("i-frames last about ", f2(frames, 2), " s (", ticks, " ticks)"));
	check(p->action != nullptr || p->iframes == 0.0, "the dash outlasts the i-frames");
	check(_hit(p, _hit_info(o, 10.0, 5.0)) == "hit", "after the i-frames a hit lands");
}

// ================================================================ guard facing

FF_TEST_F(test_combat_rules, CR, test_guard_blocks_from_the_front_with_reduced_damage) {
	const GuardRow open = _guard_trial(0.0, false);
	near(open.health, 88.0, 1e-9, "unguarded stone deals its 12 damage");
	check(open.hit == 1 && open.block == 0, "unguarded: a hit, no block");
	const GuardRow g = _guard_trial(0.0, true);
	check(g.block == 1 && g.hit == 0, S("guarded from the front: blocked (block ", g.block, " hit ", g.hit, ")"));
	check(g.health > open.health && g.health < 100.0, S("chip damage only (", f2(g.health, 2), ")"));
	near(100.0 - g.health, 12.0 * 0.12, 1e-6, "block takes 12% of the damage");
	check(100.0 - g.balance < 24.0, S("and less than the full balance damage (", f2(100.0 - g.balance, 1), ")"));
	check(g.perfect == 0, "a guard that has been up for 0.67 s is not a perfect guard");
}

FF_TEST_F(test_combat_rules, CR, test_guard_fails_when_the_attack_comes_from_behind) {
	const GuardRow back = _guard_trial(180.0, true);
	check(back.hit == 1 && back.block == 0, S("guarded from behind: a clean hit (hit ", back.hit, " block ", back.block, ")"));
	near(back.health, 88.0, 1e-9, "full damage from behind");
}

FF_TEST_F(test_combat_rules, CR, test_guard_facing_cone_boundary) {
	// The guard covers every direction with dot(forward, direction to attacker) > -0.15.
	std::string rows;
	for (int angle = 0; angle <= 180; angle += 15) {
		const GuardRow g = _guard_trial(static_cast<double>(angle), true);
		const bool covered = std::cos(deg_to_rad(static_cast<double>(angle))) > -0.15;
		rows += S(rows.empty() ? "" : " ", angle, ":", g.block > 0 ? "block" : "hit");
		check((g.block > 0) == covered, S("angle ", angle, ": block=", g.block > 0, ", expected ", covered));
		check((g.hit > 0) == !covered, S("angle ", angle, ": hit=", g.hit > 0, ", expected ", !covered));
	}
	note(rows);
	// Right at the edge of the cone (cos = -0.15 -> 98.6 degrees).
	const GuardRow inside = _guard_trial(97.0, true);
	const GuardRow outside = _guard_trial(100.0, true);
	check(inside.block > 0 && outside.hit > 0, "97 degrees still blocks, 100 degrees does not");
}

FF_TEST_F(test_combat_rules, CR, test_perfect_guard_negates_a_non_earth_stone_completely) {
	// Fire guard pressed 3 ticks before contact: perfect deflect, no damage at all.
	const GuardRow g = _guard_trial(0.0, true, 6);   // guard up 8 ticks (0.13 s) before contact
	check(g.perfect >= 1, "perfect_deflect reported");
	near(g.health, 100.0, 1e-9, "a perfect guard takes no damage");
	check(g.hit == 0 && g.block == 0, "neither a hit nor a plain block");
	// 14 ticks (0.23 s) of guard before contact is outside the 0.18 s window: an ordinary block.
	const GuardRow early = _guard_trial(0.0, true, 0);
	check(early.perfect == 0 && early.block == 1, "a guard raised 0.23 s before contact is only a block");
	near(early.health, 100.0 - 12.0 * 0.12, 1e-6, "with chip damage");
}

// ================================================================ perfect Earth guard

FF_TEST_F(test_combat_rules, CR, test_perfect_earth_guard_redirects_the_stone_to_the_thrower) {
	std::vector<EarthRow> rows;
	std::vector<double> dists;
	double d = 2.0;
	while (d <= 9.01) {
		rows.push_back(_earth_trial(d, 6.0));
		dists.push_back(d);
		d += 0.5;
	}
	int first = -1, last = -1;
	for (int k = 0; k < static_cast<int>(rows.size()); ++k) {
		if (rows[static_cast<size_t>(k)].redirects > 0) {
			if (first < 0) first = k;
			last = k;
		}
	}
	check(first >= 0, "some timing redirects the stone");
	if (first < 0) return;
	for (int k = first; k <= last; ++k)
		check(rows[static_cast<size_t>(k)].redirects > 0, S("the perfect window is contiguous (dist ", f2(dists[static_cast<size_t>(k)], 1), ")"));
	for (int k = first; k <= last; ++k) {
		const EarthRow& r = rows[static_cast<size_t>(k)];
		const std::string dl = S("dist ", f2(dists[static_cast<size_t>(k)], 1));
		check(r.redirects == 1, dl + ": exactly one redirect");
		check(r.p_hits == 0, dl + ": the guard took no damage");
		check(r.o_hits >= 1, S(dl, ": the redirected stone hit the thrower (closest ", f2(r.closest, 2), " m)"));
		const BodyRef& s = r.stone;
		check(s->attack_owner == p->id || s->attack_id == 0, dl + ": the stone changed hands");
		near(s->mass, 20.0, 1e-9, dl + ": stone mass unchanged");
	}
	note(S("perfect redirect window: stone launched ", f2(dists[static_cast<size_t>(first)], 1), "..", f2(dists[static_cast<size_t>(last)], 1), " m away"));
	// Pressing guard much earlier than the window: the wall just absorbs the stone.
	const EarthRow& early = rows.back();
	check(early.redirects == 0, "an early guard is not perfect");
	check(early.p_hits == 0 && early.walls_blocked >= 1, "the early guard's wall blocks the stone");
	check(early.o_hits == 0, "and nothing is sent back");
	// Pressing too late: the stone arrives before the wall exists.
	const EarthRow& late = rows.front();
	check(late.redirects == 0 || late.p_hits == 0, "a guard that is too late never both fails and redirects");
}

FF_TEST_F(test_combat_rules, CR, test_redirected_stone_reaches_a_thrower_at_duel_distance) {
	// The lab's fighters start 14 m apart; a redirect must be able to cross a 12 m duel.
	double best = 99.0;
	bool hit = false;
	double d = 3.0;
	while (d <= 4.6) {
		const EarthRow r = _earth_trial(d, 12.0);
		if (r.redirects > 0) {
			best = minf(best, r.closest);
			hit = hit || r.o_hits > 0;
		}
		d += 0.25;
	}
	check(best < 99.0, "setup: some timing redirects");
	check(hit, S("a redirected stone must be able to hit a thrower 12 m away (it came within ", f2(best, 2), " m)"));
}

FF_TEST_F(test_combat_rules, CR, test_wall_blocked_stone_does_not_grind_the_wall_down) {
	// Sweep guard timings so that stones reach a wall in every stage of its rise. A single 20 kg stone must never crumble
	// the wall or register more than one impact.
	std::vector<std::string> bad;
	double d = 2.0;
	while (d <= 10.01) {
		for (bool mash : {false, true}) {
			const EarthRow r = _earth_trial(d, 12.0, mash);
			if (r.walls_blocked > 2 || r.crumbles > 0)
				bad.push_back(S(f2(d, 2), mash ? "m" : "", "(blocks ", r.walls_blocked, ", crumbles ", r.crumbles, ")"));
		}
		d += 0.25;
	}
	std::string joined;
	for (size_t i = 0; i < bad.size(); ++i) joined += (i ? ", " : "") + bad[i];
	check(bad.empty(), "stones stuck against / inside a rising wall and ground it down: " + joined);
}

FF_TEST_F(test_combat_rules, CR, test_inert_stone_never_damages_a_wall) {
	// Only attacks hurt walls: an inert stone that ends up inside a raised wall's volume must not register impacts every
	// tick (and must not hang frozen in mid-air).
	_duel(Sim::EARTH, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -12));
	SimHarness& h = this->h();
	h.press(p, "guard");
	h.step(15);
	BodyRef wall = keep(h.w->get_body(p->wall_body));
	check(wall != nullptr && wall->wall_rise >= 1.0, "setup: a full wall");
	if (wall == nullptr) return;
	BodyRef s = keep(h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, wall->pos + V3(0, 0.9, 0), "scenario"));
	check(s->attack_id == 0, "setup: an inert stone inside the wall volume");
	const double start_y = static_cast<double>(s->pos.y);
	h.step(120);
	check(wall->alive && wall->wall_damage < 0.05, S("the inert stone did not grind the wall (damage ", f2(wall->wall_damage, 3), ", alive ", wall->alive, ")"));
	const int wall_blocks = h.count_events("block", [](const Dict& e) { return ev_s(e, "kind") == "wall"; });
	check(wall_blocks <= 1, S("no repeated block events (", wall_blocks, ")"));
	check(!s->alive || static_cast<double>(s->pos.y) < start_y - 0.2,
	      S("the stone is not frozen in mid-air (y ", f2(start_y, 2), " -> ", f2(static_cast<double>(s->pos.y), 2), ")"));
}

// ================================================================ guard mashing

FF_TEST_F(test_combat_rules, CR, test_guard_mashing_never_yields_a_perfect_deflect) {
	int control_perfect = 0, mashed_perfect = 0, mashed_trials = 0;
	double d = 2.0;
	while (d <= 9.01) {
		const EarthRow single = _earth_trial(d, 6.0, false);
		control_perfect += single.redirects;
		const EarthRow m = _earth_trial(d, 6.0, true);
		mashed_perfect += m.redirects;
		mashed_trials += 1;
		check(m.redirects == 0, S("mashed guard, stone from ", f2(d, 2), " m: no perfect deflect"));
		d += 0.25;
	}
	check(control_perfect > 0, S("control: a single clean guard does get perfect deflects over the same sweep (", control_perfect, ")"));
	check(mashed_trials > 10 && mashed_perfect == 0, S("no mashed sweep point was perfect (", mashed_perfect, " of ", mashed_trials, ")"));
}

FF_TEST_F(test_combat_rules, CR, test_guard_mash_lock_boundary) {
	// Two guard presses closer than GUARD_MASH_LOCK flag the second guard; a longer gap does not.
	struct Case {
		int gap;
		bool mashed;
	};
	for (const Case& c : {Case{12, true}, Case{18, true}, Case{26, false}, Case{40, false}}) {
		_duel(Sim::FIRE, Sim::EARTH, V3(0, 0, 0), V3(0, 0, -14));
		SimHarness& h = this->h();
		h.press(p, "guard");
		h.step();
		h.release(p, "guard");
		h.step(c.gap - 1);
		h.press(p, "guard");
		h.step();
		check(p->guarding, S("gap ", c.gap, ": second guard is up"));
		const bool mashed = p->action != nullptr && dbool(p->action->data, "mashed", false);
		check(mashed == c.mashed, S("gap ", c.gap, " ticks (", f2(static_cast<double>(c.gap) * Sim::DT, 2), " s): mashed=", mashed, ", expected ", c.mashed));
		check(h.w->perfect_guard(*p) == !c.mashed, S("gap ", c.gap, ": perfect window available=", h.w->perfect_guard(*p)));
	}
}

// ================================================================ element switching

FF_TEST_F(test_combat_rules, CR, test_switching_element_during_recovery_keeps_the_recovery_and_changes_the_next_action) {
	// Baseline: when does a tap earth attack finish?
	_duel(Sim::EARTH);
	{
		SimHarness& h = this->h();
		h.press(p, "attack");
		h.step();
		h.release(p, "attack");
		h.until([&] { return p->action == nullptr; }, 120);
	}
	const int64_t done_tick = _last_done_tick();
	// Same attack, but switch element while the action is in recovery.
	_duel(Sim::EARTH);
	SimHarness& h = this->h();
	h.press(p, "attack");
	h.step();
	h.release(p, "attack");
	const int reached = h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 120);
	check(reached > 0, "the attack reached recovery");
	if (reached <= 0) return;
	ActionRef inst = p->action;
	const double t_before = inst->t;
	h.element(p, Sim::FIRE);
	h.step();
	check(p->element == Sim::FIRE, "the selected element changes immediately");
	check(p->action == inst && inst->phase == ActionPhase::Recovery, "the running recovery is not cancelled");
	check(inst->id == "earth_attack" && inst->element == Sim::EARTH, "the running action keeps its own element");
	check(inst->t > t_before, S("recovery keeps ticking (", f2(t_before, 3), " -> ", f2(inst->t, 3), ")"));
	check(!h.has_event("interrupt"), "nothing was interrupted");
	h.until([&] { return p->action == nullptr; }, 60);
	const int64_t done2 = _last_done_tick();
	check(done2 == done_tick, S("recovery ends on the same tick as without the switch (", static_cast<long long>(done2), " vs ",
	                            static_cast<long long>(done_tick), ")"));
	// The next action uses the new element.
	h.press(p, "attack");
	h.step();
	check(p->action != nullptr && p->action->id == "fire_attack", S("next attack is a fire attack (", p->action ? p->action->id : std::string("none"), ")"));
	h.release(p, "attack");
	h.step(30);
}

FF_TEST_F(test_combat_rules, CR, test_element_switch_while_charging_does_not_change_the_running_action) {
	_duel(Sim::EARTH);
	SimHarness& h = this->h();
	h.press(p, "attack");
	h.step(5);
	h.element(p, Sim::WATER);
	h.step();
	check(p->element == Sim::WATER, "switched");
	check(p->action != nullptr && p->action->id == "earth_attack", "the attack in progress stays an earth attack");
	h.release(p, "attack");
	h.until([&] { return h.has_event("launch"); }, 60);
	const Dict launch = h.last_event("launch");
	const std::string kind = launch.get("kind").is_nil() ? std::string("stone") : ev_s(launch, "kind");
	check(kind != "ice", "it launches a stone, not ice");
	MatBody* b = h.w->get_body(ev_i(launch, "body", -1));
	check(b != nullptr && b->is_stone(), "the thrown body is stone");
}

FF_TEST_F(test_combat_rules, CR, test_element_switch_rules) {
	_duel(Sim::EARTH);
	SimHarness& h = this->h();
	h.element(p, Sim::EARTH);
	h.step();
	check(!h.has_event("element"), "selecting the current element is a no-op");
	p->elements[static_cast<size_t>(Sim::AIR)] = false;
	h.element(p, Sim::AIR);
	h.step();
	check(p->element == Sim::EARTH && !h.has_event("element"), "a locked element cannot be selected");
	h.element(p, Sim::WATER);
	h.step();
	check(p->element == Sim::WATER && h.events("element").size() == 1, "an unlocked element can");
}

// ================================================================ input buffer

FF_TEST_F(test_combat_rules, CR, test_attack_pressed_in_recovery_is_buffered_only_within_buffer_time) {
	_duel(Sim::EARTH);
	_tap_attack();
	h().step(120);
	const std::vector<Dict> done = _actions([&](const Dict& e) {
		return ev_i(e, "actor", -1) == p->id && ev_s(e, "move") == "earth_attack" && ev_s(e, "phase") == "done";
	});
	check(done.size() == 1, "baseline: one finished action");
	if (done.empty()) return;
	const int64_t finish_tick = ev(done[0], "tick").as_int();
	const int window = static_cast<int>(std::floor(Moves::BUFFER_TIME * Sim::HZ));
	std::vector<bool> started;
	std::string rows;
	for (int k = 0; k < 25; ++k) {
		const int64_t t = _buffer_trial(k, finish_tick);
		started.push_back(t >= 0);
		rows += S(rows.empty() ? "" : " ", k, ":", t >= 0 ? "run" : "drop");
		if (t >= 0)
			check(t == finish_tick + 1, S("pressed ", k, " ticks before the end: starts right when recovery ends (tick ", static_cast<long long>(t), ", end ",
			                              static_cast<long long>(finish_tick), ")"));
	}
	note(rows);
	int threshold = -1;
	for (int k = 0; k < static_cast<int>(started.size()); ++k) {
		if (!started[static_cast<size_t>(k)]) {
			threshold = k;
			break;
		}
	}
	check(threshold > 0, "presses far from the end are not buffered");
	for (int k = std::max(threshold, 0); k < static_cast<int>(started.size()); ++k)
		check(!started[static_cast<size_t>(k)], S("once the buffer window is exceeded it stays exceeded (k=", k, ")"));
	check(std::abs(threshold - window) <= 1, S("buffer window is BUFFER_TIME (", window, " ticks): first dropped press at k=", threshold));
	for (int k = 0; k < std::max(threshold - 2, 0); ++k) check(started[static_cast<size_t>(k)], S("pressed ", k, " ticks before the end: executes"));
}

FF_TEST_F(test_combat_rules, CR, test_press_after_recovery_starts_immediately_and_stale_buffer_is_cleared) {
	_duel(Sim::EARTH);
	SimHarness& h = this->h();
	_tap_attack();
	h.step(10);
	h.press(p, "attack");   // far too early: buffered, then dropped
	h.step();
	h.release(p, "attack");
	check(p->buffered == "attack", "the early press is buffered");
	h.step(20);
	check(p->buffered.empty(), "the stale buffered press was cleared");
	check(_actions([&](const Dict& e) {
		      return ev_i(e, "actor", -1) == p->id && ev_s(e, "move") == "earth_attack" && ev_s(e, "phase") == "startup";
	      }).size() == 1,
	      "and never executed");
	h.until([&] { return p->action == nullptr; }, 120);
	auto startups = [&] { return _actions([&](const Dict& e) { return ev_i(e, "actor", -1) == p->id && ev_s(e, "phase") == "startup"; }).size(); };
	const size_t before = startups();
	_tap_attack();
	h.step();
	check(startups() == before + 1, "a press with nothing running starts at once");
}

FF_TEST_F(test_combat_rules, CR, test_guard_and_evade_cancel_recovery_only_after_the_cancel_fraction) {
	// earth_attack: recovery 0.30 s, cancel 0.6. A guard/evade press cancels only in the last 40%.
	const double cancel = earth_attack("cancel");
	const double rec = earth_attack("recovery");
	for (const char* press_what : {"guard", "evade"}) {
		for (double frac : {0.2, 0.9}) {
			_duel(Sim::EARTH);
			SimHarness& h = this->h();
			_tap_attack();
			h.until([&] { return p->action != nullptr && p->action->phase == ActionPhase::Recovery; }, 120);
			ActionRef inst = p->action;
			if (!check(inst != nullptr, "setup: the attack reached recovery")) continue;
			while (inst->t < rec * frac - 0.5 * Sim::DT && p->action == inst) h.step();
			h.press(p, press_what);
			h.step();
			const std::string want_reason = std::string("cancel:") + press_what;
			const bool cancelled = h.any_event("interrupt", [&](const Dict& e) { return ev_i(e, "actor", -1) == p->id && ev_s(e, "reason") == want_reason; });
			const bool expected = frac >= cancel;
			const std::string pct = f2(frac * 100.0, 0);
			check(cancelled == expected, S(press_what, " at ", pct, "% of recovery: cancelled=", cancelled, ", expected ", expected));
			if (expected)
				check((p->action != nullptr && p->action->id == press_what) || std::string(press_what) == "evade", S(press_what, " action started"));
			h.release(p, press_what);
			h.step(60);
			check(p->action == nullptr && p->stun == 0.0, S(press_what, " at ", pct, "%: back to idle"));
		}
	}
}

// ================================================================ knockdown & getup

FF_TEST_F(test_combat_rules, CR, test_knockdown_getup_and_return_to_idle) {
	_duel();
	SimHarness& h = this->h();
	p->balance = 30.0;
	const std::string res = _hit(p, _hit_info(o, 5.0, 30.0));
	check(res == "knockdown", S("balance reaching 0 knocks down (", res, ")"));
	check(p->stun_kind == "knockdown" && p->stun > 0.0, S("stunned, kind knockdown (", p->stun_kind, " ", f2(p->stun, 2), ")"));
	check(p->balance == 45.0, S("balance is restored for the get-up (", f2(p->balance, 0), ")"));
	check(p->action == nullptr, "nothing running");
	// A press while down is not executed and does not linger.
	h.step(5);
	h.press(p, "attack");
	h.step();
	h.release(p, "attack");
	check(p->action == nullptr, "cannot act while knocked down");
	int ticks = 0, getup_tick = -1, free_tick = -1;
	bool iframes_during_getup = true;
	while (ticks < 400) {
		h.step();
		ticks += 1;
		if (p->stun_kind == "getup") {
			if (getup_tick < 0) getup_tick = ticks;
			if (p->iframes <= 0.0) iframes_during_getup = false;
		}
		if (free_tick < 0 && p->stun == 0.0 && p->stun_kind.empty()) {
			free_tick = ticks;
			break;
		}
	}
	check(getup_tick > 0, "a getup phase follows the knockdown");
	check(h.has_event("getup"), "getup event emitted");
	check(iframes_during_getup, "getting up grants i-frames");
	check(free_tick > getup_tick, "then the actor is free");
	const double total = static_cast<double>(free_tick + 6) * Sim::DT;
	check(total > 1.5 && total < 2.4, S("knockdown (1.1 s) + getup (0.75 s): free after ", f2(total, 2), " s"));
	check(p->action == nullptr && p->stun == 0.0 && p->iframes == 0.0,
	      S("idle state after getting up (action ", p->action ? p->action->id : std::string("<null>"), " stun ", f2(p->stun, 2), " iframes ", f2(p->iframes, 2), ")"));
	check(p->buffered.empty(), "no stale buffered input");
	const double hp0 = p->health;
	check(_hit(p, _hit_info(o, 5.0, 5.0)) == "hit", "vulnerable again after the getup");
	check(p->health < hp0, "damage applies again");
	h.step(40);
	h.press(p, "attack");
	h.step();
	check(p->action != nullptr, "and the actor can act again");
}

FF_TEST_F(test_combat_rules, CR, test_guard_break_ends_the_guard_and_recovers_cleanly) {
	_duel(Sim::FIRE);
	SimHarness& h = this->h();
	h.press(p, "guard");
	h.step(30);
	check(p->guarding && p->action != nullptr && p->action->id == "guard", "setup: P is guarding");
	const std::string r1 = _hit(p, _hit_info(o, 10.0, 100.0));
	check(r1 == "block", S("first heavy hit is blocked (", r1, ")"));
	check(p->guarding && p->stun == 0.0, "the guard holds after one blocked hit");
	near(p->balance, 100.0 - 100.0 * 0.55, 1e-6, S("a block costs 55% of the balance damage (", f2(p->balance, 1), ")"));
	const std::string r2 = _hit(p, _hit_info(o, 10.0, 100.0));
	check(r2 == "guard_break", S("the second one breaks the guard (", r2, ")"));
	check(!p->guarding && p->action == nullptr, "the guard is gone and the action interrupted");
	check(p->stun_kind == "guard_break" && p->stun > 0.0, "stunned by the guard break");
	check(p->balance == 35.0, S("balance partially restored (", f2(p->balance, 0), ")"));
	h.release(p, "guard");
	const int free = h.until([&] { return p->stun == 0.0; }, 120);
	check(free > 0 && free < 60, S("free again after the guard-break stun (", free, " ticks)"));
	h.step(2);
	check(p->action == nullptr && !p->guarding && p->stun_kind.empty(), "clean idle state");
}

FF_TEST_F(test_combat_rules, CR, test_hits_during_getup_are_evaded) {
	_duel();
	SimHarness& h = this->h();
	p->balance = 10.0;
	_hit(p, _hit_info(o, 5.0, 10.0));
	h.until([&] { return p->stun_kind == "getup"; }, 200);
	check(p->stun_kind == "getup", "reached the getup");
	const double hp0 = p->health;
	check(_hit(p, _hit_info(o, 30.0, 60.0)) == "evaded", "a hit during the getup is evaded");
	near(p->health, hp0, 1e-9, "no damage while getting up");
	check(p->stun_kind == "getup", "the getup is not interrupted");
	h.until([&] { return p->stun == 0.0; }, 120);
	check(p->stun == 0.0 && p->action == nullptr, "free afterwards");
}

FF_TEST_F(test_combat_rules, CR, test_knockdown_threshold_is_exact) {
	struct Case {
		double bal;
		const char* expected;
	};
	for (const Case& c : {Case{30.0, "knockdown"}, Case{29.99, "heavy"}, Case{10.0, "light"}}) {
		_duel();
		p->balance = 30.0;
		const std::string res = _hit(p, _hit_info(o, 1.0, c.bal));
		const std::string expected = c.expected;
		if (expected == "knockdown") {
			check(res == "knockdown" && p->stun_kind == "knockdown", S("balance 30 - 30 -> knockdown (", res, ")"));
		} else {
			check(res == "hit" && p->stun_kind == expected, S("balance 30 - ", f2(c.bal, 2), " -> ", expected, " stagger (", res, ", ", p->stun_kind, ")"));
			check(p->balance > 0.0, S("balance stays positive (", f2(p->balance, 2), ")"));
		}
	}
}

FF_TEST_F(test_combat_rules, CR, test_hit_while_knocked_down_still_ends_in_a_free_actor) {
	_duel();
	SimHarness& h = this->h();
	p->balance = 10.0;
	_hit(p, _hit_info(o, 5.0, 10.0));
	h.step(20);
	_hit(p, _hit_info(o, 5.0, 5.0));   // hit while lying down
	_hit(p, _hit_info(o, 5.0, 40.0));
	const int free = h.until([&] { return p->stun == 0.0 && p->action == nullptr; }, 600);
	check(free > 0, S("the actor eventually gets free (", free, " ticks)"));
	check(p->stun_kind.empty() || (p->stun_kind == "getup" && p->stun == 0.0), S("no dangling stun kind (", p->stun_kind, ")"));
	check(p->balance > 0.0 && p->health > 0.0, S("balance ", f2(p->balance, 1), " health ", f2(p->health, 1)));
	h.press(p, "attack");
	h.step();
	check(p->action != nullptr, "and can act");
}

// ================================================================ Focus bounds

FF_TEST_F(test_combat_rules, CR, test_spend_focus_never_goes_negative) {
	_duel();
	SimHarness& h = this->h();
	p->focus = 10.0;
	check(!h.w->spend_focus(*p, 50.0), "cannot spend more than available");
	near(p->focus, 10.0, 1e-9, "a refused spend changes nothing");
	check(h.w->spend_focus(*p, 10.0), "can spend exactly everything");
	check(p->focus == 0.0, S("focus is exactly 0, not negative (", f2(p->focus, 12), ")"));
	check(h.w->spend_focus(*p, 0.0) && h.w->spend_focus(*p, -3.0), "non-positive spends are free");
	check(p->focus == 0.0, "still 0");
	check(!h.w->spend_focus(*p, 0.5), "nothing left to spend");
	p->focus = 5.0;
	const double paid = h.w->pay_heat(*p, 1000.0);   // partial payment must stop at 0 Focus
	check(p->focus >= 0.0, S("pay_heat never drives focus below 0 (", f2(p->focus, 6), ")"));
	near(paid, 50.0, 1e-6, S("paid only what 5 Focus can buy (", f2(paid, 2), " HU)"));
	check(h.w->pay_heat(*p, 1000.0, false) == 0.0 && p->focus >= 0.0, "a refused full payment costs nothing");
}

FF_TEST_F(test_combat_rules, CR, test_focus_regen_delay_rate_and_cap) {
	_duel();
	SimHarness& h = this->h();
	h.w->spend_focus(*p, 40.0);
	const double f0 = p->focus;
	near(f0, 60.0, 1e-9, "setup: 60 Focus");
	h.step(static_cast<int>(Sim::FOCUS_REGEN_DELAY * Sim::HZ) - 3);
	near(p->focus, f0, 1e-9, "no regeneration during the delay");
	h.step(10);
	check(p->focus > f0, S("regeneration starts after the delay (", f2(p->focus, 2), ")"));
	const double f1 = p->focus;
	h.step(60);
	near(p->focus - f1, Sim::FOCUS_REGEN, 0.3, S("idle regen is ", f2(Sim::FOCUS_REGEN, 0), " per second (", f2(p->focus - f1, 2), ")"));
	h.step(60 * 10);
	check(p->focus == Sim::FOCUS_MAX, S("regen stops at FOCUS_MAX (", f2(p->focus, 4), ")"));
	// A new spend restarts the delay.
	h.w->spend_focus(*p, 10.0);
	h.step(10);
	near(p->focus, Sim::FOCUS_MAX - 10.0, 1e-9, "spending restarts the delay");
}

FF_TEST_F(test_combat_rules, CR, test_low_focus_actions_fizzle_without_negative_focus) {
	// Earth attack costs 7 Focus: with 3 it fizzles and takes nothing from the ground.
	{
		_duel(Sim::EARTH);
		SimHarness& h = this->h();
		p->focus = 3.0;
		p->focus_idle = 0.0;
		h.press(p, "attack");
		h.step();
		h.release(p, "attack");
		h.step(60);
		check(h.any_event("insufficient", [&](const Dict& e) { return ev_i(e, "actor", -1) == p->id && ev_s(e, "what") == "focus"; }),
		      "told about the missing Focus");
		check(h.events("rip").empty() && h.w->mass_ledger.ground_taken == 0.0, "no stone was ripped without Focus");
		check(p->focus >= 3.0, S("the fizzle did not spend Focus (", f2(p->focus, 3), ")"));
		check(p->action == nullptr, "the fizzled action finished");
	}
	// Exactly the cost works.
	{
		_duel(Sim::EARTH);
		SimHarness& h = this->h();
		p->focus = earth_attack("cost");
		p->focus_idle = 0.0;
		h.press(p, "attack");
		h.step();
		h.release(p, "attack");
		h.step(30);
		check(h.has_event("rip"), "exactly 7 Focus is enough");
		check(p->focus >= 0.0 && p->focus < 1.0, S("and leaves none (", f2(p->focus, 3), ")"));
	}
	// Heavy surcharge: 10 Focus pays the 7 base but not the extra 7.
	_duel(Sim::EARTH);
	SimHarness& h = this->h();
	p->focus = 10.0;
	p->focus_idle = 0.0;
	h.press(p, "attack");
	h.step(60);
	check(p->focus >= 0.0, S("focus stays >= 0 while charging a heavy (", f2(p->focus, 3), ")"));
	MatBody* b = h.w->held(*p);
	check(b != nullptr && std::fabs(b->mass - 20.0) < 1e-9, S("the heavy gather failed: the stone stays 20 kg (", b ? b->mass : -1.0, ")"));
	h.release(p, "attack");
	h.step(60);
	check(p->focus >= 0.0 && p->action == nullptr, "clean exit");
}

FF_TEST_F(test_combat_rules, CR, test_evade_and_fire_with_no_focus) {
	{
		_duel();
		SimHarness& h = this->h();
		p->focus = 0.0;
		p->focus_idle = 0.0;
		h.press(p, "evade");
		h.step();
		check(p->iframes > 0.0, "an evade with no Focus still works (i-frames)");
		check(p->focus >= 0.0, S("and does not drive Focus negative (", f2(p->focus, 3), ")"));
		h.step(60);
	}
	// Fire attack needs 60 HU: no Focus and no reserve -> fizzle.
	{
		_duel();
		SimHarness& h = this->h();
		p->focus = 0.0;
		p->focus_idle = 0.0;
		h.press(p, "attack");
		h.step();
		h.release(p, "attack");
		h.step(40);
		check(h.any_event("insufficient", [&](const Dict& e) { return ev_i(e, "actor", -1) == p->id; }), "a flare without Focus or reserve is refused");
		check(!h.has_event("flare") && p->focus >= 0.0, "no flare, focus >= 0");
	}
	// With a full heat reserve the same attack is paid from the reserve first, Focus untouched.
	_duel();
	SimHarness& h = this->h();
	p->focus = 0.0;
	p->focus_idle = 0.0;
	p->heat_reserve = 100.0;
	h.press(p, "attack");
	h.step();
	h.release(p, "attack");
	h.step(40);
	check(h.has_event("flare"), "reserve alone can pay for a flare");
	near(p->heat_reserve, 100.0 - 60.0 - Sim::RESERVE_DISSIPATE * 40.0 * Sim::DT, 1.5, S("reserve paid the 60 HU (", f2(p->heat_reserve, 1), " left)"));
	check(p->focus >= 0.0 && h.w->ledger.generated == 0.0, "no Focus was converted to heat");
}

FF_TEST_F(test_combat_rules, CR, test_focus_stays_in_bounds_under_button_mashing) {
	_duel(Sim::EARTH, Sim::FIRE);
	SimHarness& h = this->h();
	p->focus = 25.0;
	Rng rng;
	rng.set_seed(99);
	double lowest = 999.0, highest = -999.0;
	const char* kinds[] = {"attack", "tech", "guard", "evade"};
	for (int k = 0; k < 1500; ++k) {
		if (k % 7 == 0) h.press(p, kinds[rng.randi() % 4u]);
		if (k % 11 == 0) {
			h.release(p, "attack");
			h.release(p, "tech");
			h.release(p, "guard");
		}
		if (k % 400 == 0) h.element(p, static_cast<int>(rng.randi() % 4u));
		h.step();
		lowest = minf(lowest, p->focus);
		highest = maxf(highest, p->focus);
		if (p->focus < 0.0 || p->focus > Sim::FOCUS_MAX + 1e-9) {
			check(false, S("focus ", f2(p->focus, 4), " out of bounds at tick ", static_cast<long long>(h.w->tick)));
			break;
		}
	}
	check(lowest >= 0.0 && highest <= Sim::FOCUS_MAX + 1e-9,
	      S("focus stayed within 0..", f2(Sim::FOCUS_MAX, 0), " (min ", f2(lowest, 3), " max ", f2(highest, 3), ")"));
	check(lowest < 25.0, S("the mashing actually spent Focus (min ", f2(lowest, 2), ")"));
}
