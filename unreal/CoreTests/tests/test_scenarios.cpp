// Port of game/tests/sim/test_scenarios.gd: every lab scenario builds, runs 40 s of AI + random player input, can be reset
// repeatedly, and keeps its invariants (bounded bodies, conserved mass, balanced energy). Progression persistence is the
// host's job in the port (to_json / from_json instead of a user:// file).
#include "ff_test.h"
#include "sim_harness.h"

#include "AI/AiBrain.h"
#include "App/Progression.h"
#include "App/Scenarios.h"
#include "Util/GdUtil.h"
#include "Util/Rng.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

using namespace ff;
using namespace fft;

namespace {
bool finite3(Vec3 v) {
	return std::isfinite(static_cast<double>(v.x)) && std::isfinite(static_cast<double>(v.y)) && std::isfinite(static_cast<double>(v.z));
}

std::string f2(double v, int prec) {
	char b[64];
	std::snprintf(b, sizeof(b), "%.*f", prec, v);
	return b;
}

void random_intent(Rng& rng, ActorIntent& it) {
	it.attack_pressed = false;
	it.attack_released = false;
	it.guard_pressed = false;
	it.evade_pressed = false;
	it.tech_pressed = false;
	it.tech_released = false;
	it.tech_cancel = false;
	it.element_select = -1;
	if (static_cast<double>(rng.randf()) < 0.08) {
		const float x = rng.randf_range(-1.0f, 1.0f);
		const float z = rng.randf_range(-1.0f, 1.0f);
		it.move = Vec3(x, 0.0f, z).limit_length(1.0f);
	}
	const double r = static_cast<double>(rng.randf());
	if (r < 0.01) {
		it.element_select = rng.randi_range(0, 3);
	} else if (r < 0.03) {
		it.attack_pressed = true;
		it.attack_held = true;
	} else if (r < 0.04) {
		it.guard_pressed = true;
		it.guard_held = true;
	} else if (r < 0.045) {
		it.evade_pressed = true;
	} else if (r < 0.06) {
		it.tech_pressed = true;
		it.tech_held = true;
	}
	if (it.attack_held && static_cast<double>(rng.randf()) < 0.05) {
		it.attack_held = false;
		it.attack_released = true;
	}
	if (it.guard_held && static_cast<double>(rng.randf()) < 0.05) it.guard_held = false;
	if (it.tech_held && static_cast<double>(rng.randf()) < 0.03) {
		it.tech_held = false;
		it.tech_released = true;
	}
}

struct RunResult {
	Scenarios::Built built;
	std::string bad;
	int max_bodies = 0;
	double dE = 0.0, dS = 0.0, dW = 0.0;
};

RunResult run_scenario(const std::string& id, uint64_t seed_value, int ticks) {
	Progression pr;
	RunResult res;
	res.built = Scenarios::build(id, pr, seed_value);
	CombatWorld& w = *res.built.world;
	ActorState* p = res.built.player;
	ActorState* o = res.built.opponent;
	std::unique_ptr<AiBrain> ai = o != nullptr ? std::make_unique<AiBrain>(w, *o, res.built.ai_cfg, seed_value) : nullptr;
	Rng rng;
	rng.set_seed(seed_value);
	ActorIntent it;
	const double e0 = w.system_energy();
	const double s0 = w.stone_mass();
	const double w0 = w.water_mass();
	for (int k = 0; k < ticks; ++k) {
		random_intent(rng, it);
		std::map<int, ActorIntent> intents;
		intents[p->id] = it;
		if (ai) intents[o->id] = ai->think(Sim::DT);
		w.step(intents);
		w.take_events();
		res.max_bodies = std::max(res.max_bodies, w.alive_count());
		for (const auto& a : w.actors)
			if (!finite3(a->pos)) res.bad = "non-finite actor position";
		for (const BodyRef& b : w.bodies)
			if (b->alive && (!finite3(b->pos) || b->mass < -1e-6)) res.bad = "bad body " + b->describe();
	}
	res.dE = w.system_energy() - e0 - w.ledger_balance();
	res.dS = w.stone_mass() - s0;
	res.dW = w.water_mass() - w0;
	return res;
}

bool same_ints(const Value& v, std::initializer_list<int64_t> want) {
	if (!v.is_array()) return false;
	const Array& a = v.as_array();
	if (a.size() != want.size()) return false;
	size_t i = 0;
	for (int64_t x : want)
		if (a.get(i++).as_int() != x) return false;
	return true;
}

std::array<bool, 4> bools(bool a, bool b, bool c, bool d) { return {{a, b, c, d}}; }
}  // namespace

FF_TEST(test_scenarios, test_every_scenario_runs_with_invariants) {
	for (const Value& s : Scenarios::LIST()) {
		const std::string id = dstr(s.as_dict(), "id");
		const RunResult res = run_scenario(id, 5, 2400);
		check(res.bad.empty(), id + ": " + res.bad);
		check(res.max_bodies <= Sim::MAX_BODIES, S(id, ": body cap (", res.max_bodies, ")"));
		// Scenario "vent" lava and launcher stones are external sources recorded in the ledgers.
		check(std::fabs(res.dE) < 1.0, S(id, ": energy ledger balances (off by ", f2(res.dE, 3), " HU)"));
		check(std::fabs(res.dS) < 1e-3, S(id, ": stone mass conserved (off by ", f2(res.dS, 4), " kg)"));
		check(std::fabs(res.dW) < 1e-3, S(id, ": water mass conserved (off by ", f2(res.dW, 4), " kg)"));
	}
}

FF_TEST(test_scenarios, test_repeated_resets_do_not_accumulate) {
	std::vector<int> counts;
	for (int k = 0; k < 12; ++k) {
		const RunResult res = run_scenario("molten_exchange", static_cast<uint64_t>(100 + k), 300);
		counts.push_back(res.built.world->alive_count());
	}
	std::string text = "[";
	for (size_t i = 0; i < counts.size(); ++i) text += S(i ? ", " : "", counts[i]);
	text += "]";
	check(*std::max_element(counts.begin(), counts.end()) <= Sim::MAX_BODIES, "fresh worlds stay bounded " + text);
	// Each reset is a brand-new world: body ids restart, nothing carries over.
	const Scenarios::Built r1 = Scenarios::build("molten_exchange", Progression(), 1);
	const Scenarios::Built r2 = Scenarios::build("molten_exchange", Progression(), 1);
	check(r1.world->bodies.size() == r2.world->bodies.size(), "reset yields identical initial state");
}

FF_TEST(test_scenarios, test_lab_scenario_has_everything_unlocked) {
	Progression pr;
	const Scenarios::Built r = Scenarios::build("lab", pr, 1);
	ActorState* p = r.player;
	ActorState* o = r.opponent;
	check(o != nullptr, "the Lab has a rival (it throws what the spawner launches)");
	if (o == nullptr) return;
	for (size_t e = 0; e < 4; ++e) {
		check(p->elements[e] && o->elements[e], S("element ", static_cast<int>(e), " unlocked for both"));
		for (size_t s = 0; s < 4; ++s) check(p->subs_unlocked[e][s], S(kElementNames[e], "/", kSubNames[e][s], " unlocked"));
	}
	const std::vector<std::string>& omit = Progression::LAB_OMIT();
	for (const std::string& t : Progression::ALL()) {
		if (std::find(omit.begin(), omit.end(), t) != omit.end()) {
			// The bolt lives on Fire / Lightning in the Lab; a long Flame hold grows to Fire Column / Inferno.
			check(!p->has(t), "legacy flag " + t + " left out so Flame T2/T3 are reachable");
			continue;
		}
		check(p->has(t), "player technique " + t);
	}
	check(!Moves::resolve(2, 2, "strike").empty() && Moves::resolve(2, 2, "strike") != "fire_attack", "the bolt is on Fire / Lightning");
	int dummies = 0;
	for (const auto& a : r.world->actors)
		if (a->is_dummy) dummies += 1;
	check(dummies >= 2, "dummies to hit");
	check(dbool(r.def, "lab", false), "flagged as the lab");
	check(dbool(ddict(r.def, "opponent"), "ai_default", true) == false, "the rival starts passive in the Lab");
}

FF_TEST(test_scenarios, test_every_legacy_scenario_id_is_kept) {
	std::vector<std::string> ids;
	for (const Value& s : Scenarios::LIST()) ids.push_back(dstr(s.as_dict(), "id"));
	for (const char* want : {"molten_exchange", "spar", "stone_rain", "boulder", "lava_paths", "contest", "water_ice", "conduction", "redirect",
	                         "cold_hands", "updraft", "lab"})
		check(std::find(ids.begin(), ids.end(), want) != ids.end(), S("scenario ", want, " exists"));
}

FF_TEST(test_scenarios, test_spar_picks_difficulty_and_kit) {
	Progression pr;
	const Scenarios::Built base = Scenarios::build("spar", pr, 1);
	check(base.opponent->elements == bools(true, false, true, false), "default rival: earth + fire");
	check(dstr(base.ai_cfg, "preset") == "adept", "default difficulty");
	pr.spar_difficulty = "master";
	pr.spar_kit = "water";
	const Scenarios::Built r = Scenarios::build("spar", pr, 1);
	ActorState* o = r.opponent;
	check(o->elements == bools(false, true, false, false), "water kit");
	check(o->element == 1, "starts on water");
	check(dstr(r.ai_cfg, "preset") == "master", "difficulty reaches the AI config");
	check(same_ints(r.ai_cfg.get("elements"), {1}), "AI kit");
	pr.spar_kit = "fire/blue";
	const Scenarios::Built r2 = Scenarios::build("spar", pr, 1);
	const Dict subs = ddict(r2.ai_cfg, "subs");
	check(subs.size() == 1 && same_ints(subs.get("2"), {1}), "one sub-element");
	pr.spar_kit = "all";
	check(Scenarios::build("spar", pr, 1).opponent->elements == bools(true, true, true, true), "all four");
	// Other scenarios are unchanged by the spar choice.
	const Scenarios::Built me = Scenarios::build("molten_exchange", pr, 1);
	check(!me.ai_cfg.has("preset"), "legacy rivals keep their legacy config");
	// Practice list carries the pickers; choices persist.
	const std::vector<PracticeItem> items = Scenarios::practice_items(pr);
	const PracticeItem* spar_item = nullptr;
	for (const PracticeItem& it : items)
		if (it.id == "spar") spar_item = &it;
	check(spar_item != nullptr && spar_item->options.size() == 2, "spar offers difficulty and kit");
	const std::string saved = pr.to_json();
	Progression back;
	check(back.from_json(saved), "the saved progress parses");
	check(back.spar_kit == "all", "kit persists");
	check(back.spar_difficulty == "master", "difficulty persists");
}

FF_TEST(test_scenarios, test_ai_configure_is_guarded_and_accepts_the_spar_config) {
	Progression pr;
	pr.spar_kit = "air";
	pr.spar_difficulty = "novice";
	Scenarios::Built r = Scenarios::build("spar", pr, 3);
	AiBrain ai(*r.world, *r.opponent, r.ai_cfg, 3);
	ai.configure(r.ai_cfg);
	for (int k = 0; k < 600; ++k) {
		std::map<int, ActorIntent> intents;
		intents[r.player->id] = ActorIntent();
		intents[r.opponent->id] = ai.think(Sim::DT);
		r.world->step(intents);
		r.world->take_events();
	}
	check(finite3(r.opponent->pos), "the configured rival runs");
}
