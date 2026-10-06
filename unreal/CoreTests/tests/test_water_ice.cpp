// Port of game/tests/sim/test_water_ice.gd: water and ice: waterskin, ice lance (held water attack), shatter provenance,
// melting into puddles, merging and the puddle cap, drawing from the pool, and fire onto a water shield. The invariant
// behind most checks: CombatWorld::water_mass() never changes (pool + bodies + steam + waterskins + vapor/evaporated
// ledgers), except when a test spawns water itself.
#include "ff_test.h"
#include "sim_harness.h"

#include "Sim/Thermal.h"
#include "Util/GdUtil.h"
#include "Util/GodotMath.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
BodyRef keep(MatBody* b) { return b != nullptr ? b->shared_from_this() : nullptr; }

std::string ivec(const std::vector<int>& v) {
	std::string o = "[";
	for (size_t i = 0; i < v.size(); ++i) o += (i ? ", " : "") + std::to_string(v[i]);
	return o + "]";
}

double def_num(const char* move, const char* key) { return dnum(Moves::defs().get(move).as_dict(), key); }

struct WI : HarnessCase {
	ActorState* w = nullptr;   // water fighter
	ActorState* e = nullptr;   // target (Earth, idle)

	void _setup(Vec3 w_pos = V3(0, 0, 6), Vec3 e_pos = V3(0, 0, -6), uint64_t seed_value = 3) {
		SimHarness& hh = H(seed_value);
		w = hh.actor("W", w_pos, 0, Dict(), Sim::WATER);
		e = hh.actor("E", e_pos, 1, Dict(), Sim::EARTH);
		hh.step(20);   // actors turn to face each other
		hh.log.clear();
	}
	// Steps n ticks and reports (once) if water_mass() ever drifts from `base`.
	bool _step_conserved(int n, double base, const std::string& what) {
		for (int k = 0; k < n; ++k) {
			h().step();
			const double d = h().w->water_mass() - base;
			if (std::fabs(d) > 1e-6) {
				char b[64];
				std::snprintf(b, sizeof(b), "%.6f", d);
				check(false, S(what, ": water mass drifted by ", b, " kg at tick ", static_cast<long long>(h().w->tick)));
				return false;
			}
		}
		return true;
	}
	// Holds the water attack long enough for an ice lance and returns the shard (or null).
	BodyRef _lance(int hold_ticks = 40) {
		SimHarness& hh = h();
		hh.press(w, "attack");
		hh.step(hold_ticks);
		hh.release(w, "attack");
		const int t = hh.until([&] { return hh.has_event("launch"); }, 60);
		if (t <= 0) return nullptr;
		return keep(hh.w->get_body(ev_i(hh.last_event("launch"), "body", -1)));
	}
	std::vector<BodyRef> _water_bodies_except_pool() {
		std::vector<BodyRef> out;
		for (const BodyRef& b : h().w->bodies)
			if (b->alive && b->mat == Mat::Water && b->form != Form::Pool) out.push_back(b);
		return out;
	}
	std::vector<BodyRef> _puddles() {
		std::vector<BodyRef> out;
		for (const BodyRef& b : h().w->bodies)
			if (b->alive && b->form == Form::Puddle) out.push_back(b);
		return out;
	}

	struct Duel {
		double base = 0.0, e0 = 0.0, mass0 = 0.0;
		BodyRef shield;
	};
	Duel _fire_duel(bool shield) {
		SimHarness& hh = H(3);
		w = hh.actor("W", V3(0, 0, 3), 0, Dict(), Sim::WATER);
		ActorState* f = hh.actor("F", V3(0, 0, -1), 1, Dict(), Sim::FIRE);
		e = f;
		hh.step(20);
		hh.log.clear();
		if (shield) {
			hh.press(w, "guard");
			hh.step(20);
		}
		Duel d;
		d.base = hh.w->water_mass();
		d.e0 = hh.w->system_energy();
		d.shield = keep(hh.w->held(*w));
		d.mass0 = d.shield != nullptr ? d.shield->mass : 0.0;
		hh.press(f, "attack");
		hh.step();
		hh.release(f, "attack");
		hh.until([&] { return hh.has_event("flare"); }, 60);
		hh.step(2);
		return d;
	}
};
}  // namespace

// ---------------------------------------------------------------- ice lance

FF_TEST_F(test_water_ice, WI, test_held_water_attack_creates_frozen_shard_from_waterskin) {
	_setup();
	SimHarness& h = this->h();
	const double base = h.w->water_mass();
	const double e0 = h.w->system_energy();
	const double focus0 = w->focus;
	check(w->water_carried == 6.0, "waterskin starts full");
	BodyRef shard = _lance();
	check(shard != nullptr, "a held water attack launches something");
	if (shard == nullptr) return;
	check(ev_s(h.last_event("launch"), "kind") == "ice", "launch event says ice");
	check(shard->form == Form::Shard && shard->phase == Phase::Frozen,
	      S("the body is a frozen shard (", Sim::form_name(shard->form), "/", Sim::phase_name(shard->phase), ")"));
	check(shard->liquid == 0.0 && shard->temp < 0.0, S("ice: liquid 0, below freezing (", shard->temp, ")"));
	near(shard->mass, def_num("water_attack", "shard_mass"), 1e-9, "shard takes the configured mass");
	check(shard->origin == S("waterskin:", w->id), S("provenance: drawn from W's waterskin (", shard->origin, ")"));
	near(w->water_carried, 6.0 - shard->mass, 1e-9, "waterskin lost exactly the shard mass");
	check(shard->attack_owner == w->id && shard->attack_id != 0, "shard is W's attack");
	check(shard->damage == def_num("water_attack", "heavy_damage"), "heavy damage");
	near(h.w->water_mass(), base, 1e-9, "no water created or destroyed by freezing");
	// Cost: attack (5) + heavy surcharge (5) Focus; freezing dumps heat to the environment.
	const double cost = def_num("water_attack", "heavy_cost");
	check(w->focus <= focus0 - cost + 1.0, S("Focus was spent (", focus0, " -> ", w->focus, ")"));
	near(h.w->system_energy() - e0, h.w->ledger_balance(), 0.01, "energy ledger balances after freezing");
	check(h.w->ledger.freeze_dump < 0.0, "freezing is recorded as heat dumped to the environment");
}

FF_TEST_F(test_water_ice, WI, test_tap_water_attack_is_a_lash_not_a_shard) {
	_setup(V3(0, 0, -3.5), V3(0, 0, -6));
	SimHarness& h = this->h();
	h.press(w, "attack");
	h.step();
	h.release(w, "attack");
	h.step(40);
	check(h.has_event("lash"), "a tap lashes");
	check(!h.has_event("launch"), "a tap never launches a shard");
	check(_water_bodies_except_pool().empty(), "no ice body exists");
	near(w->water_carried, 6.0, 1e-9, "a lash does not consume the waterskin");
}

FF_TEST_F(test_water_ice, WI, test_ice_lance_needs_water) {
	_setup();
	SimHarness& h = this->h();
	w->water_carried = 0.9;   // below the 1 kg minimum, not standing in the pool
	h.press(w, "attack");
	h.step(40);
	h.release(w, "attack");
	h.step(60);
	check(h.any_event("insufficient", [&](const Dict& x) { return ev_i(x, "actor", -1) == w->id && ev_s(x, "what") == "water"; }),
	      "told there is not enough water");
	check(!h.has_event("launch"), "no shard without water");
	near(w->water_carried, 0.9, 1e-9, "waterskin untouched");
	check(w->action == nullptr, "no stuck action after the fizzle");
}

FF_TEST_F(test_water_ice, WI, test_ice_lance_takes_what_the_waterskin_has) {
	_setup();
	SimHarness& h = this->h();
	w->water_carried = 1.0;   // exactly the minimum: a 1 kg shard
	const double base = h.w->water_mass();
	BodyRef shard = _lance();
	check(shard != nullptr, "1.0 kg of water is enough for a lance");
	if (shard == nullptr) return;
	near(shard->mass, 1.0, 1e-9, "shard takes all the water that was there");
	near(w->water_carried, 0.0, 1e-9, "waterskin empty");
	near(h.w->water_mass(), base, 1e-9, "conserved");
	// 1 kg is the smallest shard that still splits when it shatters.
	h.until([&] { return h.has_event("shatter"); }, 120);
	const Dict split = h.last_event("split");
	check(!split.empty() && std::fabs(ev_f(split, "mass") - 0.5) < 1e-9, "a 1 kg shard splits into 0.5 kg halves");
	_step_conserved(300, base, "1 kg shard fragments");
}

FF_TEST_F(test_water_ice, WI, test_repeated_lances_drain_waterskin_then_fizzle) {
	_setup();
	SimHarness& h = this->h();
	const double base = h.w->water_mass();
	BodyRef first = _lance();
	check(first != nullptr && std::fabs(first->mass - 4.0) < 1e-9, "first lance: 4 kg");
	h.until([&] { return w->action == nullptr; }, 90);
	BodyRef second = _lance();
	check(second != nullptr && std::fabs(second->mass - 2.0) < 1e-9, "second lance: only 2 kg left");
	near(w->water_carried, 0.0, 1e-9, "waterskin empty after two lances");
	h.until([&] { return w->action == nullptr; }, 90);
	const size_t launches_before = h.events("launch").size();
	h.press(w, "attack");
	h.step(30);
	h.release(w, "attack");
	h.step(60);
	check(h.events("launch").size() == launches_before, "third lance fizzles (no water)");
	check(h.any_event("insufficient", [](const Dict& x) { return ev_s(x, "what") == "water"; }), "and reports it");
	check(_step_conserved(200, base, "after three lances"), "water conserved throughout");
}

// ---------------------------------------------------------------- shatter & provenance

FF_TEST_F(test_water_ice, WI, test_shard_impact_shatters_into_two_exact_halves_with_lineage) {
	_setup();
	SimHarness& h = this->h();
	const double base = h.w->water_mass();
	BodyRef shard = _lance();
	if (shard == nullptr) {
		check(false, "no shard");
		return;
	}
	const int sid = shard->id;
	const double shard_mass = shard->mass;
	const int t = h.until([&] { return h.has_event("shatter"); }, 150);
	check(t > 0, "the shard shatters on impact");
	check(h.events("shatter").size() == 1, S("exactly one shatter event, got ", h.events("shatter").size()));
	check(h.any_event("hit", [&](const Dict& x) { return ev_i(x, "actor", -1) == e->id && ev_i(x, "body", -1) == sid; }), "the shard hit E");
	check(e->health < 100.0, S("E took damage (", e->health, ")"));
	const std::vector<Dict> splits = h.events("split");
	check(splits.size() == 1, S("exactly one split, got ", splits.size()));
	if (splits.empty()) return;
	BodyRef parent = keep(h.w->get_body(ev_i(splits[0], "parent", -1)));
	BodyRef child = keep(h.w->get_body(ev_i(splits[0], "child", -1)));
	check(parent != nullptr && child != nullptr && parent->id == sid, "split of the shard that hit");
	if (parent == nullptr || child == nullptr) return;
	check(parent->mass + child->mass == shard_mass, S("mass is divided exactly (", parent->mass, " + ", child->mass, ")"));
	check(parent->mass == shard_mass * 0.5 && child->mass == shard_mass * 0.5, "two equal halves");
	check(child->parent_id == parent->id, "child.parent_id is the shard");
	check(child->lineage == std::vector<int>{parent->id}, S("child.lineage == [parent] (", ivec(child->lineage), ")"));
	check(child->origin == S("split:", parent->id), S("child origin names its parent (", child->origin, ")"));
	check(parent->lineage.empty(), "the shard itself was created from the waterskin (no ancestors)");
	check(child->phase == Phase::Frozen && child->temp == parent->temp && child->liquid == parent->liquid,
	      "child keeps the intensive state of the parent");
	check(parent->form == Form::Chunk && child->form == Form::Chunk, "fragments are inert chunks");
	check(parent->attack_id == 0 && child->attack_id == 0, "fragments cannot hurt anyone");
	check(parent->id != child->id && parent->alive && child->alive, "two distinct living bodies");
	// A fragment can itself be split: lineage grows by one generation.
	BodyRef grand = keep(h.w->split_body(*child, child->mass * 0.5, child->pos));
	if (!check(grand != nullptr, "the fragment splits")) return;
	check(grand->lineage == std::vector<int>{parent->id, child->id},
	      S("grandchild lineage lists both ancestors oldest first (", ivec(grand->lineage), ")"));
	near(child->mass + grand->mass + parent->mass, shard_mass, 1e-12, "mass still adds up");
	near(h.w->water_mass(), base, 1e-6, "conserved through shatter and split");
}

FF_TEST_F(test_water_ice, WI, test_missed_shard_still_shatters_exactly_once) {
	// A lone fighter with no target throws the lance straight ahead: it hits the arena instead.
	SimHarness& h = H(3);
	w = h.actor("W", V3(0, 0, -3), 0, Dict(), Sim::WATER);
	h.step(5);
	const double base = h.w->water_mass();
	BodyRef shard = _lance();
	check(shard != nullptr, "lance fired without a target");
	h.step(200);
	check(h.events("shatter").size() == 1, S("one shatter event for a shard that hit the arena, got ", h.events("shatter").size()));
	check(h.events("hit").empty(), "nobody was hit");
	check(h.events("split").size() == 1, "one split");
	check(_water_bodies_except_pool().size() <= 2, S("no duplicate fragments (", _water_bodies_except_pool().size(), " bodies)"));
	near(h.w->water_mass(), base, 1e-6, "conserved");
}

FF_TEST_F(test_water_ice, WI, test_evaded_shard_passes_through_then_breaks_on_the_ground) {
	_setup();
	SimHarness& h = this->h();
	e->iframes = 5.0;   // E is untouchable for the whole flight
	const double base = h.w->water_mass();
	BodyRef shard = _lance();
	check(shard != nullptr, "lance fired");
	h.step(150);
	check(h.has_event("evaded"), "E evaded the shard");
	check(e->health == 100.0, "no damage while evading");
	check(h.events("shatter").size() == 1, "the shard still shatters exactly once, on the arena");
	near(h.w->water_mass(), base, 1e-6, "conserved");
}

// ---------------------------------------------------------------- melting & puddles

FF_TEST_F(test_water_ice, WI, test_fragments_melt_into_puddle_and_water_mass_is_conserved_every_tick) {
	_setup();
	SimHarness& h = this->h();
	const double base = h.w->water_mass();
	BodyRef shard = _lance();
	check(shard != nullptr, "lance fired");
	if (shard == nullptr) return;
	bool ok = _step_conserved(100, base, "flight and impact");
	check(h.has_event("shatter"), "shard shattered");
	// Fragments exist and are frozen.
	const std::vector<BodyRef> frozen = _water_bodies_except_pool();
	check(frozen.size() == 2, S("two fragments after the shatter (", frozen.size(), ")"));
	for (const BodyRef& f : frozen)
		check(f->phase == Phase::Frozen && f->form == Form::Chunk, S("fragment #", f->id, " is frozen ice"));
	// Let them thaw (ice warms from the air); check conservation every tick.
	int ticks = 0;
	while (ok && ticks < 1500) {
		h.step();
		ticks += 1;
		if (std::fabs(h.w->water_mass() - base) > 1e-6) {
			ok = false;
			check(false, S("water mass drifted by ", h.w->water_mass() - base, " kg while melting (tick ", ticks, ")"));
		}
		bool any_frozen = false;
		for (const BodyRef& b : _water_bodies_except_pool())
			if (b->phase == Phase::Frozen) any_frozen = true;
		if (!any_frozen) break;
	}
	check(ticks < 1500, S("the fragments thawed within 25 s (", ticks, " ticks)"));
	const int melts = h.count_events("transform", [](const Dict& x) {
		return ev_s(x, "from") == "ice" && ev_s(x, "to") == "water" && ev_s(x, "why") == "melted";
	});
	check(melts == 2, S("two melt transforms, got ", melts));
	const std::vector<BodyRef> left = _water_bodies_except_pool();
	check(left.size() == 1 && left[0]->form == Form::Puddle, S("the two fragments became one merged puddle (", left.size(), " bodies)"));
	if (left.size() == 1) {
		near(left[0]->mass, shard->mass, 1e-9, "the puddle holds the whole shard mass");
		check(left[0]->absorbed.size() == 1, "the merge recorded the absorbed fragment");
		check(left[0]->phase == Phase::Liquid, "puddle is liquid (conductive)");
	}
	check(h.w->mass_ledger.evaporated == 0.0, "nothing evaporated");
	near(h.w->water_mass(), base, 1e-6, "conserved after melting");
}

FF_TEST_F(test_water_ice, WI, test_puddles_merge_when_touching_and_not_when_apart) {
	SimHarness& h = H(2);
	BodyRef a1 = keep(h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(0.0, 1.0, 5.0), "scenario"));
	BodyRef a2 = keep(h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(0.3, 1.0, 5.0), "scenario"));
	BodyRef b1 = keep(h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(-8.0, 1.0, 5.0), "scenario"));
	const double base = h.w->water_mass();
	h.step(60);
	const std::vector<BodyRef> ps = _puddles();
	check(ps.size() == 2, S("two touching blobs merged, the far one stayed separate (", ps.size(), " puddles)"));
	BodyRef merged;
	for (const BodyRef& p : ps)
		if (!p->absorbed.empty()) merged = p;
	check(merged != nullptr, "one puddle recorded an absorbed body");
	if (merged != nullptr) {
		near(merged->mass, 6.0, 1e-9, "merged mass is the sum");
		check(merged->absorbed.size() == 1 && (merged->absorbed[0] == a1->id || merged->absorbed[0] == a2->id), "absorbed id is the other blob");
		check(merged->radius > 0.3, S("merged puddle is larger (", merged->radius, ")"));
	}
	check(b1->alive && b1->form == Form::Puddle && std::fabs(b1->mass - 3.0) < 1e-9, "the distant puddle is untouched");
	near(h.w->water_mass(), base, 1e-9, "conserved");
}

FF_TEST_F(test_water_ice, WI, test_puddles_are_capped_and_evaporation_is_accounted) {
	SimHarness& h = H(2);
	const int n = 14;
	for (int i = 0; i < n; ++i) {
		const double x = -14.0 + 3.0 * static_cast<double>(i % 7);
		const double z = i < 7 ? 7.0 : 5.0;
		h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(x, 1.0, z), "scenario");
	}
	const double base = h.w->water_mass();
	size_t max_seen = 0;
	for (int k = 0; k < 120; ++k) {
		h.step();
		max_seen = std::max(max_seen, _puddles().size());
		if (std::fabs(h.w->water_mass() - base) > 1e-6) {
			check(false, S("water mass drifted by ", h.w->water_mass() - base, " kg at tick ", static_cast<long long>(h.w->tick)));
			break;
		}
	}
	const size_t cap = static_cast<size_t>(Sim::MAX_PUDDLES);
	check(max_seen <= cap, S("never more than MAX_PUDDLES (", Sim::MAX_PUDDLES, ") puddles, saw ", max_seen));
	check(_puddles().size() == cap, S("exactly MAX_PUDDLES remain when more landed (", _puddles().size(), ")"));
	const int evaporated = h.count_events("despawn", [](const Dict& x) { return ev_s(x, "reason") == "evaporated"; });
	check(evaporated == n - Sim::MAX_PUDDLES, S("the overflow puddles evaporated (", evaporated, ")"));
	near(h.w->mass_ledger.evaporated, 3.0 * static_cast<double>(n - Sim::MAX_PUDDLES), 1e-9, "evaporated mass is recorded in the ledger");
	near(h.w->water_mass(), base, 1e-6, "water mass conserved with the evaporation ledger");
}

FF_TEST_F(test_water_ice, WI, test_water_falling_into_the_pool_merges_into_it) {
	SimHarness& h = H(2);
	const double pool_before = h.w->pool->mass;
	BodyRef blob = keep(h.w->spawn_body(Mat::Water, Form::Stream, 3.0, V3(10.0, 1.0, -1.0), "scenario"));
	const double base = h.w->water_mass();
	h.step(60);
	near(h.w->pool->mass, pool_before + 3.0, 1e-9, "the pool gained exactly the blob's mass");
	check(!blob->alive && blob->mass == 0.0, "the blob is gone");
	check(_puddles().empty(), "no puddle left behind");
	near(h.w->water_mass(), base, 1e-9, "conserved");
}

// ---------------------------------------------------------------- drawing water

FF_TEST_F(test_water_ice, WI, test_water_technique_draws_exactly_what_leaves_the_pool) {
	_setup(V3(5, 0, -4), V3(-6, 0, -4));
	SimHarness& h = this->h();
	MatBody* pool = h.w->pool;
	const double pool0 = pool->mass;
	const double total0 = h.w->water_mass();
	h.press(w, "tech");
	const double rate = def_num("water_tech", "draw_rate") * Sim::DT;
	double prev = 0.0;
	int drawing_ticks = 0;
	bool bad_step = false;
	for (int k = 0; k < 40; ++k) {
		h.step();
		MatBody* held = h.w->held(*w);
		if (held == nullptr) continue;
		if (!near(pool0 - pool->mass, held->mass, 1e-9, S("tick ", static_cast<long long>(h.w->tick), ": pool lost exactly what the stream gained")))
			break;
		if (drawing_ticks > 0 && !bad_step) {
			if (std::fabs((held->mass - prev) - rate) > 1e-9) {
				bad_step = true;
				check(false, S("tick ", static_cast<long long>(h.w->tick), ": draw increment ", held->mass - prev, ", want rate ", rate));
			}
		}
		prev = held->mass;
		drawing_ticks += 1;
	}
	check(drawing_ticks >= 10, S("water was being drawn for ", drawing_ticks, " ticks"));
	MatBody* held = h.w->held(*w);
	check(held != nullptr && held->is_water() && held->phase == Phase::Liquid, "W holds a liquid water body");
	if (held != nullptr) {
		check(held->origin == S("draw:", pool->id), S("provenance: drawn from the pool (", held->origin, ")"));
		check(std::find(held->lineage.begin(), held->lineage.end(), pool->id) != held->lineage.end(), "lineage includes the pool");
	}
	near(h.w->water_mass(), total0, 1e-9, "conserved while drawing");
	check(h.has_event("draw_water"), "draw_water event emitted");
	check(w->water_carried == 6.0, "the waterskin was not touched while the pool was in reach");
}

FF_TEST_F(test_water_ice, WI, test_water_draw_never_exceeds_max_draw) {
	_setup(V3(5, 0, -4), V3(-6, 0, -4));
	SimHarness& h = this->h();
	const double pool0 = h.w->pool->mass;
	h.press(w, "tech");
	h.step(150);   // well past the time needed to reach max_draw
	MatBody* held = h.w->held(*w);
	check(held != nullptr, "W is still holding water");
	if (held == nullptr) return;
	const double cap = def_num("water_tech", "max_draw");
	check(held->mass <= cap + 1e-6, S("held water ", held->mass, " kg exceeds max_draw ", cap, " kg"));
	near(pool0 - h.w->pool->mass, held->mass, 1e-9, "pool lost exactly the held mass");
}

FF_TEST_F(test_water_ice, WI, test_released_stream_hits_once_and_becomes_a_puddle_with_all_its_mass) {
	_setup(V3(5, 0, -4), V3(-5, 0, -4));
	SimHarness& h = this->h();
	const double total0 = h.w->water_mass();
	h.press(w, "tech");
	h.step(30);
	BodyRef stream = keep(h.w->held(*w));
	check(stream != nullptr, "holding drawn water");
	if (stream == nullptr) return;
	h.release(w, "tech");
	const int tl = h.until([&] { return h.has_event("launch"); }, 30);
	check(tl > 0, "the stream is launched");
	const double mass = stream->mass;   // drawing continues until the release tick: measure at launch
	const int t = h.until([&] { return h.has_event("hit"); }, 120);
	check(t > 0, "the stream hits E");
	h.step(40);
	check(h.count_events("hit", [&](const Dict& x) { return ev_i(x, "actor", -1) == e->id; }) == 1, "exactly one hit (dedup)");
	check(e->wetness > 0.9, S("the target is soaked (", e->wetness, ")"));
	check(stream->form == Form::Puddle && stream->alive, "the stream landed as a puddle");
	near(stream->mass, mass, 1e-9, "the puddle holds the whole stream");
	near(h.w->water_mass(), total0, 1e-9, "conserved through the throw");
}

FF_TEST_F(test_water_ice, WI, test_draw_without_pool_uses_the_waterskin) {
	_setup(V3(-4, 0, 8), V3(-4, 0, -6));   // far from the pool, no puddles
	SimHarness& h = this->h();
	const double total0 = h.w->water_mass();
	h.press(w, "tech");
	h.step(30);
	MatBody* held = h.w->held(*w);
	check(held != nullptr && held->origin == S("waterskin:", w->id), "draws from the waterskin when nothing else is in reach");
	if (held != nullptr) near(held->mass, 6.0, 1e-9, "takes the whole waterskin");
	near(w->water_carried, 0.0, 1e-9, "waterskin empty");
	near(h.w->water_mass(), total0, 1e-9, "conserved");
	h.cancel_tech(w);
	h.step(120);
	check(w->action == nullptr && w->held_body == -1, "cancel leaves no stuck state");
	near(h.w->water_mass(), total0, 1e-6, "conserved after cancelling (the water falls as a puddle)");
	check(_puddles().size() == 1, "the dropped water became a puddle");
}

FF_TEST_F(test_water_ice, WI, test_draw_with_nothing_in_reach_fails_cleanly) {
	_setup(V3(-4, 0, 8), V3(-4, 0, -6));
	SimHarness& h = this->h();
	w->water_carried = 0.0;
	const double total0 = h.w->water_mass();
	h.press(w, "tech");
	h.step(60);
	check(h.any_event("insufficient", [&](const Dict& x) { return ev_i(x, "actor", -1) == w->id && ev_s(x, "what") == "water"; }),
	      "no water in reach is reported");
	check(h.w->held(*w) == nullptr, "nothing is held");
	h.release(w, "tech");
	h.step(60);
	check(w->action == nullptr && w->stun == 0.0, "no stuck action");
	near(h.w->water_mass(), total0, 1e-9, "conserved");
}

FF_TEST_F(test_water_ice, WI, test_standing_in_the_pool_refills_the_waterskin_from_the_pool) {
	_setup(V3(10, 0, -1), V3(-6, 0, -4));
	SimHarness& h = this->h();
	w->water_carried = 1.0;
	const double pool0 = h.w->pool->mass;
	const double total0 = h.w->water_mass();
	h.step(3);
	check(w->in_water, "W stands in the pool");
	near(w->water_carried, 6.0, 1e-9, "waterskin refilled");
	near(h.w->pool->mass, pool0 - 5.0, 1e-9, "the pool paid exactly what the waterskin gained");
	near(h.w->water_mass(), total0, 1e-9, "conserved");
}

// ---------------------------------------------------------------- fire vs water shield

FF_TEST_F(test_water_ice, WI, test_fire_flare_on_water_shield_makes_steam_and_blocks_damage) {
	const Duel d = _fire_duel(true);
	SimHarness& h = this->h();
	BodyRef shield = d.shield;
	check(shield != nullptr, "W raised a water shield");
	if (shield == nullptr) return;
	check(w->health == 100.0, S("the shield blocked the flame (health ", w->health, ")"));
	check(h.count_events("hit", [&](const Dict& x) { return ev_i(x, "actor", -1) == w->id; }) == 0, "no hit event on W");
	check(h.any_event("block", [&](const Dict& x) { return ev_i(x, "actor", -1) == w->id && ev_s(x, "kind") == "fire_water"; }),
	      "block(fire_water) reported");
	check(h.has_event("steam_block"), "steam_block reported");
	const double boiled = d.mass0 - shield->mass;
	check(boiled > 0.5, S("the shield lost water to steam (", boiled, " kg)"));
	check(boiled <= def_num("fire_attack", "heat") / Thermal::vapor_energy(1.0) + 1e-9, "no more water boiled than the flare's heat can vaporise");
	auto steam_clouds = [&] {
		std::vector<BodyRef> out;
		for (const BodyRef& b : h.w->bodies)
			if (b->alive && b->mat == Mat::Steam) out.push_back(b);
		return out;
	};
	const std::vector<BodyRef> clouds = steam_clouds();
	check(clouds.size() >= 1, "a steam cloud exists");
	double cloud_mass = 0.0;
	for (const BodyRef& c : clouds) cloud_mass += c->mass;
	near(cloud_mass, boiled, 1e-6, "steam carries the boiled mass (Vector2 float32 rounding allowed)");
	near(h.w->water_mass(), d.base, 1e-6, "water mass conserved while the cloud is alive");
	near(h.w->ledger.vapor, Thermal::vapor_energy(boiled), 1e-6, "vapour energy ledger matches the boiled mass");
	// The cloud dissipates; its mass moves into mass_ledger.vapor.
	check(h.w->mass_ledger.vapor == 0.0, "nothing dissipated yet");
	h.step(240);
	check(steam_clouds().empty(), "the cloud dissipated");
	near(h.w->mass_ledger.vapor, boiled, 1e-6, "mass_ledger.vapor accounts for the dissipated steam");
	near(h.w->water_mass(), d.base, 1e-6, "water mass conserved after dissipation");
	near(h.w->system_energy() - d.e0, h.w->ledger_balance(), 0.01, "energy ledger balances");
	// Letting go of the guard returns the remaining water to the waterskin.
	h.release(w, "guard");
	h.step(40);
	near(w->water_carried, d.mass0 - boiled, 1e-6, "the surviving shield water went back to the waterskin");
	near(h.w->water_mass(), d.base, 1e-6, "conserved after the guard ends");
}

FF_TEST_F(test_water_ice, WI, test_fire_flare_without_shield_hurts_the_same_target) {
	const Duel d = _fire_duel(false);
	SimHarness& h = this->h();
	check(d.shield == nullptr, "no shield without guarding");
	check(w->health < 100.0, S("unshielded W takes the flame (health ", w->health, ")"));
	check(h.any_event("hit", [&](const Dict& x) { return ev_i(x, "actor", -1) == w->id && ev_s(x, "kind") == "fire"; }), "fire hit reported");
	check(!h.has_event("steam_block"), "no steam without water in the way");
}

FF_TEST_F(test_water_ice, WI, test_small_residue_of_a_boiled_puddle_is_not_lost) {
	// A flare vaporises 36 HU / 26 = 1.385 kg of a 1.4 kg puddle; the 0.015 kg left over is below the removal threshold.
	// Whatever happens to the body, water_mass() must not drop.
	SimHarness& h = H(3);
	ActorState* f = h.actor("F", V3(0, 0, -1), 1, Dict(), Sim::FIRE);
	h.step(5);
	BodyRef puddle = keep(h.w->spawn_body(Mat::Water, Form::Puddle, 1.4, V3(0, 0, 2), "scenario"));
	puddle->update_radius_puddle();
	const double base = h.w->water_mass();
	h.press(f, "attack");
	h.step();
	h.release(f, "attack");
	h.until([&] { return h.has_event("flare"); }, 60);
	h.step(2);
	check(h.has_event("steam_block"), "the flame reached the puddle");
	check(!puddle->alive || puddle->mass < 1.4, "the puddle was boiled");
	near(h.w->water_mass(), base, 1e-6, S("no water vanished (residue ", puddle->alive ? puddle->mass : 0.0, " kg)"));
	h.step(240);
	near(h.w->water_mass(), base, 1e-6, "still conserved after the steam dissipates");
}
