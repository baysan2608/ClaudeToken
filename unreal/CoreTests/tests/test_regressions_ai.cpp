// Port of game/tests/sim/test_regressions_ai.gd: regressions for sparring-AI decision bugs: drawing waves it cannot set
// in time (or walking into them while drawing), dropped technique presses, pool-edge jitter, blind throws into cover,
// guards that drop during a visible charge, unreachable strike defence, and the per-tick contest roll.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <cmath>
#include <optional>

using namespace ff;
using namespace fft;

namespace {
// A freshly poured, fully molten wave (the fields ActFire._pour sets).
BodyRef _wave(CombatWorld& w, ActorState* owner, double mass, Vec3 p, Vec3 dir) {
	const Dict d = Moves::defs().get("pour").as_dict();
	MatBody* b = w.spawn_body(Mat::Stone, Form::Wave, mass, p, "test", Sim::STONE_MELT_C);
	b->liquid = 1.0;
	b->phase = Phase::Molten;
	b->on_ground = true;
	b->update_radius();
	b->wave_dir = dir;
	b->wave_budget = dnum(d, "base_budget") + dnum(d, "budget_per_kg") * mass;
	b->wave_width = 1.1 + mass * 0.025;
	b->wave_path.assign(1, p);
	b->max_life = -1.0;
	b->attack_id = w.new_attack_id();
	b->attack_owner = owner->id;
	b->hit_set.add(owner->id);
	b->damage = dnum(d, "damage");
	b->balance_damage = dnum(d, "balance");
	return b->shared_from_this();
}

AiRig _duel(uint64_t seed_value, Vec3 opp_pos, const Dict& cfg, const Dict& player_kit = D({{"magma", true}})) {
	AiRig r;
	r.h = std::make_unique<SimHarness>(seed_value);
	r.p = r.h->actor("player", V3(0, 0, 6), 0, player_kit, Sim::FIRE);
	r.o = r.h->actor("opponent", opp_pos, 1, D({{"heat_draw", true}}), Sim::EARTH);
	r.o->elements = {{true, false, true, false}};
	Dict c = D({{"aggression", 0.6}, {"counter", 1.0}, {"elements", A({0, 2})}, {"drill", "passive"}});
	merge_into(c, cfg);
	r.ai = std::make_unique<AiBrain>(*r.h->w, *r.o, c, seed_value);
	r.tick(5);
	return r;
}

std::string key_of(const MatBody& b) { return "b" + itos(b.id) + ":" + itos(b.attack_id); }

struct Trial {
	std::string decision;
	bool hit = false;
	double drift = 0.0;
};
// Pours a wave of `mass` straight at the AI from `dist` m; returns its decision, whether it was hit and how far it
// moved while channelling a draw (before any hit).
Trial _wave_trial(double mass, double dist, uint64_t seed_value, const Dict& cfg = Dict()) {
	const Vec3 start = V3(0, 0, 4.7);
	AiRig d = _duel(seed_value, start - V3(0, 0, dist), cfg);
	SimHarness& h = *d.h;
	ActorState* o = d.o;
	AiTestAccess::next_attack(*d.ai) = 1e9;   // free sparring: no throws of its own
	const BodyRef b = _wave(*h.w, d.p, mass, start, V3(0, 0, -1));
	const std::string key = key_of(*b);
	Trial res;
	std::optional<Vec3> at;
	for (int k = 0; k < 400; ++k) {
		d.think();
		auto& decided = AiTestAccess::decided(*d.ai);
		if (res.decision.empty() && decided.count(key)) res.decision = decided[key];
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "hit" && ev_i(e, "actor", -1) == o->id && ev_s(e, "kind") == "lava") res.hit = true;
		}
		const ActionRef a = o->action;
		if (!res.hit && a != nullptr && a->id == "fire_tech" && a->phase == ActionPhase::Channel) {
			if (!at) at = o->pos;
			res.drift = maxf(res.drift, o->pos.distance_to(*at));
		}
		if (b->form != Form::Wave) break;
	}
	return res;
}
}  // namespace

FF_TEST(test_regressions_ai, test_ai_walls_a_wave_it_cannot_draw_in_time) {
	// A 45 kg wave holds ~2.25x the heat of a 20 kg one: the draw cannot set it before contact, so the AI must pick the
	// wall (which always stops it) instead. (A close 20 kg wave is just as hopeless for a draw.)
	int hits = 0, draws = 0, n = 0;
	const double md[4][2] = {{45.0, 6.0}, {45.0, 8.4}, {45.0, 10.0}, {20.0, 6.0}};
	for (const auto& m : md) {
		for (uint64_t s : {1u, 2u, 3u}) {
			const Trial r = _wave_trial(m[0], m[1], s);
			++n;
			hits += r.hit ? 1 : 0;
			draws += r.decision == "draw" ? 1 : 0;
		}
	}
	note(S("unwinnable waves: ", hits, " hits, ", draws, " draws of ", n));
	check(hits == 0, S("waves the draw cannot set are stopped another way (", hits, "/", n, " hit)"));
	check(draws == 0, S("the AI does not pick a draw it cannot finish (", draws, "/", n, ")"));
}

FF_TEST(test_regressions_ai, test_ai_still_draws_a_wave_it_can_set) {
	// The draw stays the counter of choice when it works (20 kg from 10 m).
	int ok = 0;
	for (uint64_t s : {1u, 2u, 3u, 4u}) {
		const Trial r = _wave_trial(20.0, 10.0, s);
		if (r.decision == "draw" && !r.hit) ++ok;
	}
	check(ok == 4, S("a settable wave is drawn and stopped (", ok, "/4)"));
}

FF_TEST(test_regressions_ai, test_ai_stands_still_while_it_draws) {
	int draws = 0, bad = 0;
	double drift = 0.0;
	struct C {
		const char* drill;
		double mass, dist;
	};
	for (const C& c : {C{"passive", 45.0, 13.0}, C{"", 30.0, 10.5}, C{"", 30.0, 11.5}, C{"", 20.0, 10.0}}) {
		for (uint64_t s : {1u, 2u, 3u, 4u, 5u, 6u}) {
			const Trial r = _wave_trial(c.mass, c.dist, s, D({{"drill", c.drill}}));
			if (r.decision == "draw") {
				++draws;
				bad += r.hit ? 1 : 0;
				drift = maxf(drift, r.drift);
			}
		}
	}
	note(S("draws ", draws, ", hit through the draw ", bad, ", max drift while drawing ", drift, " m"));
	check(draws >= 8, S("setup: the draw is still chosen (", draws, "/24)"));
	check(bad == 0, S("a chosen draw sets the wave before it arrives (", bad, "/", draws, " hit)"));
	check(drift < 0.01, S("no steps while channelling the draw (", drift, " m)"));
}

FF_TEST(test_regressions_ai, test_wave_is_perceived_from_the_pour_wind_up) {
	// The pour's 0.25 s wind-up is the wave's visible telegraph (same body): the reaction clock starts there.
	for (uint64_t s : {1u, 2u, 3u}) {
		AiRig d = _duel(s, V3(0, 0, -3), Dict());
		SimHarness& h = *d.h;
		ActorState* p = d.p;
		const BodyRef b = h.w->spawn_body(Mat::Stone, Form::Blob, 20.0, p->hand_point(), "test", Sim::STONE_MELT_C)->shared_from_this();
		b->liquid = 0.85;
		b->phase = Phase::Molten;
		b->update_radius();
		b->controller = p->id;
		p->held_body = b->id;
		const Vec3 aim = V3(0, 0, -1);
		h.w->start_action(*p, "pour", h.it(p), D({{"aim", aim}, {"face", aim}, {"body", b->id}}));
		double spawn_t = -1.0, decided_t = -1.0;
		for (int k = 0; k < 60; ++k) {
			d.think();
			if (spawn_t >= 0.0 && decided_t < 0.0 && AiTestAccess::decided(*d.ai).count(key_of(*b))) decided_t = AiTestAccess::t(*d.ai);
			h.step();
			if (spawn_t < 0.0 && b->form == Form::Wave) spawn_t = AiTestAccess::t(*d.ai);
		}
		check(spawn_t >= 0.0 && decided_t >= 0.0, S("seed ", s, ": the poured wave is decided on"));
		check(decided_t - spawn_t < dnum(d.ai->cfg, "reaction") - 0.1, S("seed ", s, ": decided ", decided_t - spawn_t, " s after the wave appeared (pour seen first)"));
	}
}

FF_TEST(test_regressions_ai, test_draw_pressed_mid_throw_starts_when_the_throw_ends) {
	// The world buffers a press for 0.15 s only; a draw decided during the AI's own throw recovery used to be dropped.
	int started = 0, drew = 0;
	for (uint64_t s : {1u, 2u, 3u}) {
		AiRig d = _duel(s, V3(0, 0, -6), Dict());
		SimHarness& h = *d.h;
		ActorState* o = d.o;
		h.w->_try_start(*o, "attack", ActorIntent());   // the AI's own light stone throw
		h.until([&]() { return o->action != nullptr && o->action->phase == ActionPhase::Active; }, 60);
		// 7 m away and flowing across (inside draw range and the aim cone throughout): only the press handling is under test.
		const BodyRef b = _wave(*h.w, d.p, 20.0, V3(0, 0, 1.0), V3(1, 0, 0));
		d.think();
		AiTestAccess::start_draw(*d.ai, *b);
		h.intents[o->id] = d.ai->intent;   // GDScript shares the intent object: _start_draw's selection reaches this step
		const size_t n0 = h.log.size();
		for (int k = 0; k < 90; ++k) {
			h.step();
			d.think();
		}
		bool tech = false, drawing = false;
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "action" && ev_i(e, "actor", -1) == o->id && ev_s(e, "move") == "fire_tech") tech = true;
			if (ev_s(e, "type") == "drawing" && ev_i(e, "actor", -1) == o->id) drawing = true;
		}
		started += tech ? 1 : 0;
		drew += drawing ? 1 : 0;
	}
	check(started == 3, S("the buffered draw press is retried until the technique starts (", started, "/3)"));
	check(drew == 3, S("the draw then actually extracts heat (", drew, "/3)"));
}

FF_TEST(test_regressions_ai, test_threats_are_perceived_while_holding) {
	AiRig d = _duel(4, V3(0, 0, -6), Dict());
	SimHarness& h = *d.h;
	AiBrain& ai = *d.ai;
	AiTestAccess::hold(ai) = "attack";
	AiTestAccess::hold_until(ai) = AiTestAccess::t(ai) + 0.6;
	const BodyRef b = _wave(*h.w, d.p, 20.0, V3(0, 0, 6), V3(0, 0, -1));
	b->wave_budget = 40.0;
	const std::string key = key_of(*b);
	d.tick(24);
	check(AiTestAccess::hold(ai) == "attack" && AiTestAccess::seen(ai).count(key), "the wave is seen during the hold");
	int ticks_after = -1;
	for (int k = 0; k < 40; ++k) {
		d.tick();
		if (AiTestAccess::hold(ai).empty() && ticks_after < 0) ticks_after = 0;
		else if (ticks_after >= 0) ++ticks_after;
		if (AiTestAccess::decided(ai).count(key)) break;
	}
	check(AiTestAccess::decided(ai).count(key) && ticks_after <= 2, S("decided right after the hold (", ticks_after, " ticks after)"));
}

// ------------------------------------------------------------------ movement

namespace {
struct MoveRun {
	double rev = 0.0, min_d = kInf;
	int wet = 0;
};
MoveRun _move_run(uint64_t seed_value, Vec3 ppos, Vec3 opos, double secs) {
	SimHarness h(seed_value);
	ActorState* p = h.actor("player", ppos, 0, Dict(), Sim::EARTH);
	ActorState* o = h.actor("opponent", opos, 1, Dict(), Sim::EARTH);
	AiBrain ai(*h.w, *o, D({{"elements", Array()}, {"drill", ""}, {"aggression", 0.6}}), seed_value);
	Vec3 last;
	int rev = 0;
	MoveRun r;
	for (int k = 0; k < static_cast<int>(secs * 60); ++k) {
		const ActorIntent it = ai.think(Sim::DT);
		h.intents[o->id] = it;
		h.step();
		if (it.move.length() > 0.1f && last.length() > 0.1f && it.move.normalized().dot(last.normalized()) < -0.5f) ++rev;
		last = it.move;
		r.min_d = minf(r.min_d, o->pos.distance_to(p->pos));
		if (h.w->arena.in_pool(o->pos.x, o->pos.z)) ++r.wet;
	}
	r.rev = rev / secs;
	return r;
}
}  // namespace

FF_TEST(test_regressions_ai, test_ai_walks_around_the_pool_without_jitter) {
	const double hi = 11.0 - 2.0 * 0.6;
	for (uint64_t s : {1u, 2u, 3u}) {
		// Foe straight across the pool: the AI must go round it, not vibrate at the edge.
		const MoveRun r = _move_run(s, V3(10, 0, -9), V3(10, 0, 6), 15.0);
		note(S("across pool seed ", s, ": rev ", r.rev, " min_d ", r.min_d, " wet ", r.wet));
		check(r.rev < 1.0, S("seed ", s, ": no direction flip-flop (", r.rev, " reversals/s)"));
		check(r.min_d < hi + 0.5, S("seed ", s, ": reaches its range around the pool (closest ", r.min_d, " m)"));
		check(r.wet == 0, S("seed ", s, ": stays out of the pool (", r.wet, " ticks in it)"));
	}
	for (uint64_t s : {1u, 2u}) {
		// Strafing along the west edge with the foe at its spawn.
		const MoveRun r = _move_run(s, V3(0, 0, 7), V3(6.0, 0, -1), 15.0);
		note(S("edge strafe seed ", s, ": rev ", r.rev, " min_d ", r.min_d, " wet ", r.wet));
		check(r.rev < 1.0, S("edge seed ", s, ": no direction flip-flop (", r.rev, " reversals/s)"));
		check(r.wet == 0, S("edge seed ", s, ": stays out of the pool (", r.wet, " ticks in it)"));
	}
}

FF_TEST(test_regressions_ai, test_ai_does_not_throw_blind_from_behind_cover) {
	for (uint64_t s : {1u, 2u, 3u}) {
		SimHarness h(s);
		ActorState* p = h.actor("player", V3(0, 0, 7), 0, Dict(), Sim::EARTH);
		ActorState* o = h.actor("opponent", V3(-4.0, 0, -1.8), 1, Dict(), Sim::EARTH);
		o->elements = {{true, false, false, false}};
		AiBrain ai(*h.w, *o, D({{"drill", "stone_rain"}, {"interval", 2.4}, {"counter", 0.3}, {"aggression", 0.5}, {"elements", A({0})}}), s);
		AiTestAccess::next_attack(ai) = 0.4;
		int throws = 0, blind = 0, reached = 0;
		for (int k = 0; k < 60 * 15; ++k) {
			h.intents[o->id] = ai.think(Sim::DT);
			const size_t n0 = h.log.size();
			h.step();
			for (size_t i = n0; i < h.log.size(); ++i) {
				const Dict& e = h.log[i];
				const std::string ty = ev_s(e, "type");
				if (ty == "launch" && ev_i(e, "actor", -1) == o->id) {
					++throws;
					if (!h.w->arena.has_los(o->chest(), p->chest())) ++blind;
				} else if ((ty == "hit" || ty == "block") && ev_i(e, "actor", -1) == p->id) {
					++reached;
				}
			}
		}
		note(S("seed ", s, ": throws ", throws, " blind ", blind, " reached ", reached));
		check(blind == 0, S("seed ", s, ": no throws without line of sight (", blind, " of ", throws, ")"));
		check(reached >= 3, S("seed ", s, ": stones reach the player (", reached, " of ", throws, ")"));
	}
}

// ------------------------------------------------------------------ guards

namespace {
std::pair<std::string, std::string> _bolt_trial(uint64_t seed_value, double hold) {
	SimHarness h(seed_value);
	ActorState* p = h.actor("player", V3(0, 0, 4), 0, D({{"lightning", true}}), Sim::FIRE);
	ActorState* o = h.actor("opponent", V3(0, 0, -4), 1, D({{"heat_draw", true}}), Sim::EARTH);
	o->elements = {{true, false, true, false}};
	AiBrain ai(*h.w, *o, D({{"counter", 0.7}, {"elements", A({0, 2})}, {"aggression", 0.6}}), seed_value);
	AiTestAccess::next_attack(ai) = 1e9;
	for (int k = 0; k < 30; ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		h.step();
	}
	h.press(p, "attack");
	std::string bolt = "none";
	for (int k = 0; k < static_cast<int>((hold + 1.0) * 60); ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		if (k == static_cast<int>(hold * 60)) h.release(p, "attack");
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "lightning" && ev_i(e, "actor", -1) == p->id)
				bolt = ev_b(e, "blocked") ? "blocked" : (arr_has_int(darr(e, "hits"), o->id) ? "hit" : "miss");
		}
	}
	return {bolt, AiTestAccess::hold(ai)};
}
}  // namespace

FF_TEST(test_regressions_ai, test_bolt_guard_lasts_as_long_as_the_charge) {
	for (double hold : {1.5, 2.5}) {
		int blocked = 0;
		for (uint64_t s = 0; s < 4; ++s) {
			const auto r = _bolt_trial(700 + s, hold);
			if (r.first == "blocked") ++blocked;
			check(r.second.empty(), S("hold ", hold, " s seed ", s, ": guard released after the bolt"));
		}
		check(blocked == 4, S("a ", hold, " s charge is still blocked (", blocked, "/4)"));
	}
}

namespace {
struct StrikeRun {
	int strikes = 0, melee = 0, defended = 0, landed = 0, max_seen = 0;
};
StrikeRun _strike_run(int el, int hold_ticks, double secs, double gap = 3.0) {
	SimHarness h(3);
	ActorState* p = h.actor("player", V3(0, 0, gap * 0.5), 0, Dict(), el);
	ActorState* o = h.actor("opponent", V3(0, 0, -gap * 0.5), 1, D({{"heat_draw", true}}), Sim::EARTH);
	o->elements = {{true, false, true, false}};
	AiBrain ai(*h.w, *o, D({{"counter", 1.0}, {"elements", A({0, 2})}, {"aggression", 0.6}, {"drill", "passive"}}), 3);
	StrikeRun res;
	int held = 0;
	for (int k = 0; k < static_cast<int>(secs * 60); ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		p->focus = 100.0;
		p->heat_reserve = 500.0;
		o->health = 100.0;
		if (p->action == nullptr && p->stun <= 0.0 && held == 0) {
			h.press(p, "attack");
			++res.strikes;
			held = 1;
		} else if (held > 0) {
			++held;
			if (held > hold_ticks) {
				h.release(p, "attack");
				held = 0;
			}
		}
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			const std::string ty = ev_s(e, "type");
			if (ty == "action" && ev_i(e, "actor", -1) == o->id && (ev_s(e, "move") == "guard" || ev_s(e, "move") == "evade") && ev_s(e, "phase") == "startup")
				++res.defended;
			if (ty == "hit" && ev_i(e, "actor", -1) == o->id) ++res.landed;
		}
		res.max_seen = maxi(res.max_seen, static_cast<int>(AiTestAccess::seen(ai).size()));
	}
	for (const auto& kv : AiTestAccess::decided(ai))
		if (kv.second == "melee") ++res.melee;
	return res;
}
std::string sr(const StrikeRun& r) {
	return S("{strikes ", r.strikes, " melee ", r.melee, " defended ", r.defended, " landed ", r.landed, " max_seen ", r.max_seen, "}");
}
}  // namespace

FF_TEST(test_regressions_ai, test_ai_defends_a_held_close_strike) {
	// Held blaze / gust charges are long, visible telegraphs; taps (< 0.2 s) stay unreactable.
	for (int el : {Sim::FIRE, Sim::AIR}) {
		const StrikeRun r = _strike_run(el, 50, 12.0);
		const std::string nm = el == Sim::FIRE ? "Fire" : "Air";
		note(nm + " held: " + sr(r));
		check(r.melee > 0 && r.defended > 0, S(nm, ": held strikes are guarded or evaded (", r.melee, " decisions)"));
		check(r.landed * 2 < r.strikes, S(nm, ": most held strikes are stopped (", r.landed, " of ", r.strikes, " landed)"));
	}
	// A held blaze reaches 6.5 m: it is answered from beyond plain flare range too.
	const StrikeRun far = _strike_run(Sim::FIRE, 50, 12.0, 5.8);
	note("Fire held from 5.8 m: " + sr(far));
	check(far.melee > 0 && far.landed * 2 < far.strikes,
	      S("a held blaze from 5.8 m is defended (", far.melee, " decisions, ", far.landed, " of ", far.strikes, " landed)"));
}

FF_TEST(test_regressions_ai, test_perceived_strike_keys_stay_bounded) {
	// Each undecided tap used to leave a key in _seen forever.
	const StrikeRun r = _strike_run(Sim::WATER, 2, 50.0);
	note("water taps: " + sr(r));
	check(r.strikes > 70, S("enough strikes to overflow the table (", r.strikes, ")"));
	check(r.max_seen <= 65, S("perception table stays bounded (max ", r.max_seen, ")"));
}

// ------------------------------------------------------------------ opportunities

namespace {
bool _contest_trial(uint64_t seed_value, double counter) {
	SimHarness h(seed_value);
	ActorState* p = h.actor("player", V3(0, 0, 4), 0, D({{"magma", true}}), Sim::FIRE);
	ActorState* o = h.actor("opponent", V3(0, 0, -3), 1, D({{"heat_draw", true}}), Sim::FIRE);
	p->facing = kPi;
	o->facing = 0.0;
	MatBody* st = h.w->spawn_body(Mat::Stone, Form::Chunk, 20.0, V3(0, 0.25, 2.8), "test");
	st->on_ground = true;
	AiBrain ai(*h.w, *o, D({{"counter", counter}, {"elements", A({0, 2})}, {"aggression", 0.0}}), seed_value);
	AiTestAccess::next_attack(ai) = 1e9;
	h.press(p, "tech");
	for (int k = 0; k < 240; ++k) {
		h.intents[o->id] = ai.think(Sim::DT);
		h.intents[o->id].move = Vec3();
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			if (ev_s(e, "type") == "thermal" && ev_i(e, "actor", -1) == o->id && ev_s(e, "mode") == "DRAW") return true;
		}
	}
	return false;
}
}  // namespace

FF_TEST(test_regressions_ai, test_contest_chance_is_rolled_once_per_hold) {
	// counter * 0.35 is the chance per held stone, not per tick (which compounded to ~100%).
	int low = 0, high = 0;
	const int n = 40;
	for (int s = 0; s < n; ++s) {
		low += _contest_trial(1000 + static_cast<uint64_t>(s), 0.2) ? 1 : 0;
		high += _contest_trial(1000 + static_cast<uint64_t>(s), 1.0) ? 1 : 0;
	}
	note(S("contests: counter 0.2 -> ", low, "/", n, ", counter 1.0 -> ", high, "/", n));
	check(low <= 8, S("counter 0.2 (~7%) rarely contests (", low, "/", n, ")"));
	check(high >= 5 && high <= 26, S("counter 1.0 (~35%) contests about a third of the time (", high, "/", n, ")"));
}
