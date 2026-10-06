// Port of game/tests/sim/test_ai_duel.gd: 120 s headless AI-vs-AI duel across all four elements and sub-elements (master
// vs adept presets): the planner drives every kit through the real intent path; the run must keep the ledgers balanced,
// stay finite, leave no fighter stuck in an action or a hold, and actually exercise many moves.
#include "ai_util.h"
#include "ff_test.h"
#include "sim_harness.h"

#include "Util/GdUtil.h"

#include <cmath>
#include <map>
#include <set>

using namespace ff;
using namespace fft;

namespace {
constexpr int DUEL_TICKS = 7200;   // 120 s
constexpr int STUCK_TICKS = 300;   // 5 s busy without holding a button
constexpr int HOLD_TICKS = 600;    // 10 s holding the same button
constexpr double MASS_TOL = 1e-3;

struct Duel {
	std::unique_ptr<SimHarness> h;
	std::map<int, std::unique_ptr<AiBrain>> brains;
	std::map<std::string, int> moves, events, outcomes;
	std::set<int> elements;
	std::set<std::string> subs;
	std::map<std::string, std::vector<std::string>> violations;
	double worst_energy = 0.0;
	std::map<std::string, double> worst{{"earth", 0.0}, {"water", 0.0}, {"metal", 0.0}, {"plant", 0.0}};
	std::map<int, int> longest_busy, longest_hold, hits;
	double e0 = 0.0;
	std::map<std::string, double> m0;
	int decisions = 0;
	void add(const std::string& cat, const std::string& msg) {
		std::vector<std::string>& v = violations[cat];
		if (v.size() < 5) v.push_back(msg);
	}
};

double mass_of(CombatWorld& w, const std::string& m) {
	if (m == "earth") return w.earth_mass();
	if (m == "water") return w.water_mass();
	if (m == "metal") return w.metal_mass();
	return w.plant_mass();
}

bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}

Duel& _run(uint64_t seed_value) {
	static std::map<uint64_t, std::unique_ptr<Duel>> cache;
	auto it = cache.find(seed_value);
	if (it != cache.end()) return *it->second;
	auto dp = std::make_unique<Duel>();
	Duel& d = *dp;
	d.h = std::make_unique<SimHarness>(seed_value);
	SimHarness& h = *d.h;
	ActorState* a = h.actor("A", V3(0, 0, 7), 0, Dict(), Sim::EARTH);
	ActorState* b = h.actor("B", V3(0, 0, -7), 1, Dict(), Sim::FIRE);
	d.brains[a->id] = std::make_unique<AiBrain>(*h.w, *a, Dict(), seed_value * 2 + 1);
	d.brains[b->id] = std::make_unique<AiBrain>(*h.w, *b, Dict(), seed_value * 2 + 2);
	d.brains[a->id]->configure(D({{"preset", "master"}, {"elements", A({0, 1, 2, 3})}}));
	d.brains[b->id]->configure(D({{"preset", "adept"}, {"elements", A({0, 1, 2, 3})},
	                              {"subs", D({{"0", A({0, 1, 2, 3})}, {"1", A({0, 1, 2, 3})}, {"2", A({0, 1, 2, 3})}, {"3", A({0, 1, 2, 3})}})}}));
	CombatWorld& w = *h.w;
	d.e0 = w.system_energy() - w.ledger_balance();
	for (const char* m : {"earth", "water", "metal", "plant"}) d.m0[m] = mass_of(w, m);
	std::map<int, int> busy{{a->id, 0}, {b->id, 0}};
	std::map<int, std::pair<int, std::string>> held{{a->id, {0, ""}}, {b->id, {0, ""}}};
	for (ActorState* x : {a, b}) {
		d.longest_busy[x->id] = 0;
		d.longest_hold[x->id] = 0;
		d.hits[x->id] = 0;
	}
	std::map<int, std::string> last_plan{{a->id, ""}, {b->id, ""}};
	for (int k = 0; k < DUEL_TICKS; ++k) {
		for (ActorState* x : {a, b}) {
			AiBrain& br = *d.brains[x->id];
			h.intents[x->id] = br.think(Sim::DT);
			const std::string lp = dstr(br.last_plan, "key", "");
			if (!lp.empty() && lp != last_plan[x->id]) {
				last_plan[x->id] = lp;
				++d.decisions;
			}
		}
		const size_t n0 = h.log.size();
		h.step();
		for (size_t i = n0; i < h.log.size(); ++i) {
			const Dict& e = h.log[i];
			const std::string ty = ev_s(e, "type");
			d.events[ty] += 1;
			if (ty == "action" && ev_s(e, "phase") == "startup") {
				d.moves[ev_s(e, "move")] += 1;
			} else if (ty == "element") {
				d.elements.insert(ev_i(e, "element"));
				d.subs.insert(itos(ev_i(e, "element")) + "/" + itos(ev_i(e, "sub", 0)));
			} else if (ty == "interaction") {
				d.outcomes[ev_s(e, "outcome")] += 1;
			} else if (ty == "hit" && d.hits.count(ev_i(e, "actor", -1))) {
				d.hits[ev_i(e, "actor", -1)] += 1;
			}
		}
		// Keep both fighters in the fight: health never regenerates and 0 HP actors stop acting.
		for (ActorState* x : {a, b})
			if (x->health < 30.0) x->health = 100.0;
		// Ledgers.
		d.worst_energy = maxf(d.worst_energy, absf(w.system_energy() - w.ledger_balance() - d.e0));
		for (auto& kv : d.worst) kv.second = maxf(kv.second, absf(mass_of(w, kv.first) - d.m0[kv.first]));
		// Finite state, stuck actions, endless holds.
		for (ActorState* x : {a, b}) {
			if (!(finite3(x->pos) && std::isfinite(x->focus) && std::isfinite(x->health))) d.add("nan_actor", S(x->name, " at tick ", static_cast<long long>(w.tick)));
			if (std::fabs(x->pos.x) > 17.0f || std::fabs(x->pos.z) > 17.0f || x->pos.y < -2.0f) d.add("out_of_arena", S(x->name, " ", x->pos.x, ",", x->pos.y, ",", x->pos.z));
			const ActorIntent& itx = h.it(x);
			const bool holding = itx.attack_held || itx.guard_held || itx.tech_held || itx.evade_held;
			busy[x->id] = (x->action != nullptr && !holding && x->stun <= 0.0) ? busy[x->id] + 1 : 0;
			d.longest_busy[x->id] = maxi(d.longest_busy[x->id], busy[x->id]);
			if (busy[x->id] == STUCK_TICKS + 1) d.add("stuck_action", S(x->name, " busy 5 s in ", x->action->id, " (", x->action->phase_name(), ")"));
			const std::string hk = std::string(itx.attack_held ? "A" : "") + (itx.guard_held ? "G" : "") + (itx.tech_held ? "T" : "") + (itx.evade_held ? "E" : "");
			if (!hk.empty() && hk == held[x->id].second) held[x->id].first += 1;
			else held[x->id] = {hk.empty() ? 0 : 1, hk};
			d.longest_hold[x->id] = maxi(d.longest_hold[x->id], held[x->id].first);
			if (held[x->id].first == HOLD_TICKS + 1) d.add("endless_hold", S(x->name, " holds ", hk, " for 10 s (", d.brains[x->id]->debug_state, ")"));
		}
		for (const BodyRef& body : w.bodies)
			if (body->alive && !(std::isfinite(static_cast<double>(body->pos.x)) && std::isfinite(body->mass) && std::isfinite(body->temp)))
				d.add("nan_body", body->describe());
	}
	cache[seed_value] = std::move(dp);
	return *cache[seed_value];
}

template <class M>
std::string map_str(const M& m) {
	std::string s = "{";
	for (const auto& kv : m) s += S(kv.first, ": ", kv.second, ", ");
	return s + "}";
}
}  // namespace

FF_TEST(test_ai_duel, test_duel_runs_two_minutes_and_exercises_the_kits) {
	Duel& d = _run(31);
	check(d.h->w->tick == DUEL_TICKS, S("ran ", static_cast<long long>(d.h->w->tick), " ticks"));
	note(S("moves used (", d.moves.size(), "): ", map_str(d.moves)));
	std::string els;
	for (int e : d.elements) els += itos(e) + " ";
	note(S("elements ", els, ", subs ", d.subs.size(), ", outcomes ", map_str(d.outcomes), ", hits ", map_str(d.hits), ", counter decisions ",
	       d.decisions, ", chains ", d.events["chain"], ", weaves ", d.events["weave"], ", morphs ", d.events["morph"]));
	check(d.moves.size() >= 20, S("a broad set of moves was used (", d.moves.size(), " distinct)"));
	check(d.elements.size() >= 4, "all four elements were selected (" + els + ")");
	check(d.subs.size() >= 10, S("many sub-elements were selected (", d.subs.size(), ")"));
	check(d.events["interaction"] > 20, S("counters met threats (", d.events["interaction"], " interactions)"));
	check(d.decisions >= 20, S("both brains decided on many threats (", d.decisions, ")"));
	int hit_sum = 0;
	for (const auto& kv : d.hits) hit_sum += kv.second;
	check(hit_sum >= 5, "attacks landed (" + map_str(d.hits) + ")");
}

FF_TEST(test_ai_duel, test_duel_keeps_the_ledgers_balanced) {
	Duel& d = _run(31);
	CombatWorld& w = *d.h->w;
	note(S("worst drift: energy ", d.worst_energy, " ", map_str(d.worst)));
	check(d.worst_energy <= 0.05 * maxf(1.0, w.ledger.generated + w.ledger.ambient), S("energy ledger drift ", d.worst_energy, " HU (generated ", w.ledger.generated, ")"));
	check(d.worst["earth"] <= MASS_TOL * maxf(100.0, w.mass_ledger.ground_taken), S("earth mass drift ", d.worst["earth"], " kg of ", w.mass_ledger.ground_taken, " handled"));
	check(d.worst["water"] <= MASS_TOL * maxf(100.0, d.m0["water"]), S("water mass drift ", d.worst["water"], " kg"));
	check(d.worst["metal"] <= MASS_TOL * 100.0, S("metal mass drift ", d.worst["metal"], " kg"));
	check(d.worst["plant"] <= MASS_TOL * 100.0, S("plant mass drift ", d.worst["plant"], " kg"));
}

FF_TEST(test_ai_duel, test_duel_has_no_stuck_actions_or_endless_holds) {
	Duel& d = _run(31);
	for (const char* c : {"stuck_action", "endless_hold", "nan_actor", "nan_body", "out_of_arena"}) {
		std::string list;
		auto it = d.violations.find(c);
		if (it != d.violations.end())
			for (const std::string& m : it->second) list += m + "; ";
		check(list.empty(), std::string(c) + ": " + list);
	}
	note("longest busy " + map_str(d.longest_busy) + ", longest hold " + map_str(d.longest_hold));
}
